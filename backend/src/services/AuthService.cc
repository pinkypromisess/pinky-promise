#include "AuthService.h"

#include <argon2.h>
#include <openssl/rand.h>

#include <stdexcept>
#include <vector>

#include "../validation/AuthValidation.h"

namespace services
{
namespace
{
// OWASP's current Argon2id baseline (OWASP Password Storage Cheat Sheet's
// "Argon2id" row, m=19 MiB, t=2, p=1) -- named constants, not magic
// numbers, per this module's brief. If real-world CPU/latency profiling
// on Cloud Run later suggests otherwise, these are the only numbers that
// need to change.
constexpr uint32_t kArgon2TimeCost = 2;
constexpr uint32_t kArgon2MemoryCostKib = 19456;  // 19 MiB
constexpr uint32_t kArgon2Parallelism = 1;
constexpr uint32_t kArgon2SaltLength = 16;
constexpr uint32_t kArgon2HashLength = 32;

// Hashes `password` with Argon2id via libargon2's own reference
// implementation (argon2id_hash_encoded, see <argon2.h>) -- deliberately
// NOT hand-rolled and NOT built from OpenSSL primitives, per this
// module's brief. Returns the whole self-describing encoded string
// ("$argon2id$v=19$m=...,t=...,p=...$<salt>$<hash>"); that full string is
// what's stored in users.password_hash verbatim, salt/params are never
// split out separately.
//
// The salt itself is generated fresh per call with OpenSSL's RAND_bytes
// (a CSPRNG call, not a hashing primitive) -- argon2id_hash_encoded takes
// the salt as an input rather than generating it internally.
std::string hashPassword(const std::string &password)
{
    std::vector<unsigned char> salt(kArgon2SaltLength);
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1)
    {
        throw std::runtime_error("Failed to generate a random password salt.");
    }

    const size_t encodedLen = argon2_encodedlen(kArgon2TimeCost,
                                                 kArgon2MemoryCostKib,
                                                 kArgon2Parallelism,
                                                 kArgon2SaltLength,
                                                 kArgon2HashLength,
                                                 Argon2_id);
    std::vector<char> encoded(encodedLen);

    const int rc = argon2id_hash_encoded(kArgon2TimeCost,
                                          kArgon2MemoryCostKib,
                                          kArgon2Parallelism,
                                          password.data(),
                                          password.size(),
                                          salt.data(),
                                          salt.size(),
                                          kArgon2HashLength,
                                          encoded.data(),
                                          encoded.size());
    if (rc != ARGON2_OK)
    {
        throw std::runtime_error(std::string("argon2id_hash_encoded failed: ") +
                                  argon2_error_message(rc));
    }

    // encoded.data() is a NUL-terminated C string on success; encoded.size()
    // includes room for that NUL, which the std::string(const char*)
    // constructor below correctly does not include in the result.
    return std::string(encoded.data());
}

// Verifies `password` against `encodedHash` (a string previously produced
// by hashPassword() above) using libargon2's own argon2id_verify, which
// parses the params/salt/hash out of the encoded string itself. Any
// mismatch or malformed-hash error code is treated identically as "not a
// match" -- the caller (AuthService::login) doesn't need to distinguish
// them any further.
bool verifyPassword(const std::string &encodedHash, const std::string &password)
{
    const int rc = argon2id_verify(encodedHash.c_str(), password.data(), password.size());
    return rc == ARGON2_OK;
}

}  // namespace

AuthService::AuthService(drogon::orm::DbClientPtr db,
                          std::shared_ptr<auth::SocialTokenVerifier> googleVerifier,
                          std::shared_ptr<auth::SocialTokenVerifier> appleVerifier)
  : db_(std::move(db)),
    googleVerifier_(std::move(googleVerifier)),
    appleVerifier_(std::move(appleVerifier))
{
}

std::string AuthService::signup(const std::string &email, const std::string &password)
{
    if (!validation::isValidEmailShape(email))
    {
        throw AuthBadRequestException("EMAIL_INVALID", "Please enter a valid email address.");
    }
    if (!validation::isValidPasswordLength(password))
    {
        throw AuthBadRequestException("PASSWORD_TOO_SHORT",
                                       "Password must be at least 8 characters.");
    }

    // Checked first, as a separate read, so a taken email gets a clean 409
    // instead of a unique_violation exception bubbling up from the INSERT
    // below -- same "check first, then act" non-atomic-precondition shape
    // BlockService::createBlock uses for its own checks (acceptable at MVP
    // scale; the users_email_key UNIQUE constraint is still the real guard
    // under a concurrent-signup race).
    auto existing = db_->execSqlSync("SELECT 1 FROM users WHERE email = $1", email);
    if (!existing.empty())
    {
        throw AuthConflictException("EMAIL_ALREADY_REGISTERED",
                                     "An account with this email already exists.");
    }

    const auto passwordHash = hashPassword(password);

    auto rows = db_->execSqlSync(
        "INSERT INTO users (email, password_hash) VALUES ($1, $2) RETURNING id",
        email,
        passwordHash);
    return rows[0]["id"].as<std::string>();
}

std::string AuthService::login(const std::string &email, const std::string &password)
{
    auto rows =
        db_->execSqlSync("SELECT id, password_hash FROM users WHERE email = $1", email);

    if (rows.empty() || rows[0]["password_hash"].isNull())
    {
        // Unknown email, or a row with no password_hash (e.g. a
        // social-only account once F.2 exists) -- same exception as a
        // wrong password below, deliberately.
        throw AuthUnauthorizedException("INVALID_CREDENTIALS", "Incorrect email or password.");
    }

    const auto storedHash = rows[0]["password_hash"].as<std::string>();
    if (!verifyPassword(storedHash, password))
    {
        throw AuthUnauthorizedException("INVALID_CREDENTIALS", "Incorrect email or password.");
    }

    return rows[0]["id"].as<std::string>();
}

SocialLoginResult AuthService::socialLoginWith(const std::string &provider,
                                                const std::string &idToken,
                                                auth::SocialTokenVerifier &verifier)
{
    auto identity = verifier.verify(idToken);
    if (!identity)
    {
        throw AuthUnauthorizedException("INVALID_SOCIAL_TOKEN", "Could not verify this token.");
    }

    // One atomic statement, same data-modifying-CTEs-chained-by-subquery/
    // RETURNING idiom as BlockService::createBlock / PinkyPromiseService::
    // confirm:
    //  - existing_identity: the (provider, provider_subject) row, if this
    //    identity has signed in before.
    //  - inserted_user: only fires (INSERT ... SELECT ... WHERE NOT
    //    EXISTS) when no existing_identity row was found -- a brand new
    //    users row, password_hash left NULL (this account has no
    //    password). email is the token's verified email UNLESS a
    //    *different* users row already has that exact email (see the
    //    note below on why), in which case it's left NULL here too --
    //    either way, auth_identities.email_at_signup (always set, never
    //    NULL) is the durable record of the email this identity actually
    //    vouches for.
    //  - inserted_identity: SELECTs its user_id FROM inserted_user, so it
    //    only ever produces a row in the same "new" case as inserted_user
    //    above -- the auth_identities row for this (provider, subject).
    //  - Final SELECT: inserted_identity's row (was_created = true) UNION
    //    ALL existing_identity's row (was_created = false, guarded by
    //    NOT EXISTS(inserted_identity) so a genuinely new signup can't
    //    double-count) -- always yields exactly one row.
    //
    // No cross-provider linking: this lookup is keyed ONLY on (provider,
    // provider_subject), never on email, so the same real person signing
    // up with Google then later with Apple (even with the same email
    // both times) always produces two separate users rows -- FT-8,
    // deliberately not built here. That collides with TWO constraints on
    // the pre-existing `users` table (migration 001, owned by Module A,
    // predates F.2 and isn't touched here): email is UNIQUE, so a second
    // row can't store the identical value the first one has; AND
    // users_email_or_phone_present (email IS NOT NULL OR phone IS NOT
    // NULL) means leaving it NULL isn't an option either, since a social
    // account has no phone. Resolved by giving whichever row loses that
    // race a deterministic, guaranteed-unique PLACEHOLDER value instead
    // of a real email -- "<provider_subject>@<provider>.social-placeholder.invalid"
    // (".invalid" is RFC 2606's reserved TLD for exactly "not a real
    // address", and provider_subject is already unique per provider per
    // the UNIQUE(provider, provider_subject) constraint, so this can
    // never collide). The TRUE verified email is never lost either way --
    // it's always in this same row's auth_identities.email_at_signup
    // (NOT NULL, no uniqueness constraint). This service never reads
    // users.email for a social account, so login-by-password for that
    // email is unaffected: it still correctly resolves to whichever
    // users row actually holds it (or 401s if neither does). This is this
    // module's one real spec/schema conflict; flagged in the checkpoint
    // report rather than silently deciding it alone.
    auto rows = db_->execSqlSync(
        "WITH existing_identity AS ("
        "  SELECT user_id FROM auth_identities "
        "  WHERE provider = $1 AND provider_subject = $2"
        "), inserted_user AS ("
        "  INSERT INTO users (email) "
        "  SELECT CASE WHEN EXISTS (SELECT 1 FROM users WHERE email = $3) "
        "              THEN $2 || '@' || $1 || '.social-placeholder.invalid' "
        "              ELSE $3 END "
        "  WHERE NOT EXISTS (SELECT 1 FROM existing_identity) "
        "  RETURNING id"
        "), inserted_identity AS ("
        "  INSERT INTO auth_identities (user_id, provider, provider_subject, email_at_signup) "
        "  SELECT id, $1, $2, $3 FROM inserted_user "
        "  RETURNING user_id"
        ") "
        "SELECT user_id, true AS was_created FROM inserted_identity "
        "UNION ALL "
        "SELECT user_id, false AS was_created FROM existing_identity "
        "WHERE NOT EXISTS (SELECT 1 FROM inserted_identity)",
        provider,
        identity->subject,
        identity->email);

    const auto &row = rows[0];
    SocialLoginResult result;
    result.userId = row["user_id"].as<std::string>();
    result.wasCreated = row["was_created"].as<bool>();
    return result;
}

SocialLoginResult AuthService::signupOrLoginWithGoogle(const std::string &idToken)
{
    return socialLoginWith("google", idToken, *googleVerifier_);
}

SocialLoginResult AuthService::signupOrLoginWithApple(const std::string &idToken)
{
    return socialLoginWith("apple", idToken, *appleVerifier_);
}

}  // namespace services

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

AuthService::AuthService(drogon::orm::DbClientPtr db) : db_(std::move(db))
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

}  // namespace services

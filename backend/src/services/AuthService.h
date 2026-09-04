#pragma once

#include <drogon/orm/DbClient.h>

#include <memory>
#include <string>

#include "../auth/SocialTokenVerifier.h"
#include "AuthServiceErrors.h"

namespace services
{
// Returned by the two signupOrLoginWith*() methods below so the
// controller can pick 201 (new user) vs 200 (existing identity matched)
// without a second query -- same wasCreated pattern
// BlockService::createBlock uses.
struct SocialLoginResult
{
    std::string userId;
    bool wasCreated = false;
};

// Owns Module F's signup/login: email+password (F.1) -- Argon2id password
// hashing/verification (see AuthService.cc), duplicate-email checking --
// and Google/Apple social login (F.2) -- provider id-token verification
// (delegated to an injected auth::SocialTokenVerifier per provider) plus
// the find-or-create auth_identities/users logic. JWT issuance is
// deliberately NOT this service's job -- AuthController calls
// auth::signJwt() with the user id these methods return, so this service
// has no dependency on JwtAuth. Every DB call is synchronous
// (drogon::orm::DbClient::execSqlSync), matching every other service in
// this codebase.
class AuthService
{
  public:
    AuthService(drogon::orm::DbClientPtr db,
                std::shared_ptr<auth::SocialTokenVerifier> googleVerifier,
                std::shared_ptr<auth::SocialTokenVerifier> appleVerifier);

    // Throws:
    //  - AuthBadRequestException (400, EMAIL_INVALID / PASSWORD_TOO_SHORT)
    //    on malformed input.
    //  - AuthConflictException (409, EMAIL_ALREADY_REGISTERED) if a users
    //    row with this email already exists.
    // Returns the newly created user's id on success.
    std::string signup(const std::string &email, const std::string &password);

    // Throws AuthUnauthorizedException (401, INVALID_CREDENTIALS) for an
    // unknown email, a row with no password_hash, or a wrong password --
    // all three indistinguishable to the caller, see AuthServiceErrors.h.
    // Returns the matched user's id on success.
    std::string login(const std::string &email, const std::string &password);

    // Verifies `idToken` via the Google-configured auth::SocialTokenVerifier,
    // then find-or-creates the (users, auth_identities) rows -- see
    // AuthService.cc for the exact atomic SQL. Throws
    // AuthUnauthorizedException (401, INVALID_SOCIAL_TOKEN) if the token
    // fails verification; nothing is created in that case. No
    // cross-provider account linking: the same email via Apple always
    // produces a separate users row (see AuthService.cc's find-or-create
    // SQL, keyed on (provider, provider_subject) only -- forward task
    // FT-8, deliberately not built here).
    SocialLoginResult signupOrLoginWithGoogle(const std::string &idToken);

    // Same contract as signupOrLoginWithGoogle(), verified via the
    // Apple-configured auth::SocialTokenVerifier instead.
    SocialLoginResult signupOrLoginWithApple(const std::string &idToken);

  private:
    drogon::orm::DbClientPtr db_;
    std::shared_ptr<auth::SocialTokenVerifier> googleVerifier_;
    std::shared_ptr<auth::SocialTokenVerifier> appleVerifier_;

    SocialLoginResult socialLoginWith(const std::string &provider, const std::string &idToken,
                                       auth::SocialTokenVerifier &verifier);
};

}  // namespace services

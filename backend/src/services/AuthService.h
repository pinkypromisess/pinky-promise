#pragma once

#include <drogon/orm/DbClient.h>

#include <string>

#include "AuthServiceErrors.h"

namespace services
{
// Owns Module F.1's email+password signup/login: Argon2id password
// hashing/verification (see AuthService.cc), duplicate-email checking,
// and the users row itself. JWT issuance is deliberately NOT this
// service's job -- AuthController calls auth::signJwt() with the user id
// this returns, so this service has no dependency on the auth:: module.
// Every DB call is synchronous (drogon::orm::DbClient::execSqlSync),
// matching every other service in this codebase.
class AuthService
{
  public:
    explicit AuthService(drogon::orm::DbClientPtr db);

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

  private:
    drogon::orm::DbClientPtr db_;
};

}  // namespace services

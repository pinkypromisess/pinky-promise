#pragma once

#include <stdexcept>
#include <string>

namespace services
{
// POST /v1/auth/signup -> 400. `code` distinguishes the cases:
//   EMAIL_INVALID       -- email missing or fails the simple shape check
//                          (validation::isValidEmailShape).
//   PASSWORD_TOO_SHORT  -- password missing or under
//                          validation::kMinPasswordLength characters.
class AuthBadRequestException : public std::runtime_error
{
  public:
    AuthBadRequestException(std::string code, std::string message)
      : std::runtime_error(std::move(message)), code(std::move(code))
    {
    }

    std::string code;
};

// POST /v1/auth/signup -> 409 EMAIL_ALREADY_REGISTERED. Thrown for any
// existing users.email match, whether that row already has a
// password_hash or not (a future social-only account, added in F.2, still
// counts as "taken").
class AuthConflictException : public std::runtime_error
{
  public:
    AuthConflictException(std::string code, std::string message)
      : std::runtime_error(std::move(message)), code(std::move(code))
    {
    }

    std::string code;
};

// POST /v1/auth/login -> 401 INVALID_CREDENTIALS. Deliberately thrown with
// the exact same code/message for all three of: no user with this email,
// a user row with no password_hash (e.g. a social-only account), and a
// password that doesn't match the stored hash -- see
// AuthService::login. Letting a client distinguish "no such email" from
// "wrong password" is a user-enumeration vector.
class AuthUnauthorizedException : public std::runtime_error
{
  public:
    AuthUnauthorizedException(std::string code, std::string message)
      : std::runtime_error(std::move(message)), code(std::move(code))
    {
    }

    std::string code;
};

}  // namespace services

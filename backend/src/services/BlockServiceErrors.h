#pragma once

#include <stdexcept>
#include <string>

namespace services
{
// POST /v1/blocks -> 400. `code` distinguishes the cases:
//   CANNOT_BLOCK_SELF -- blocked_user_id == the caller.
//   USER_NOT_FOUND    -- blocked_user_id is malformed or doesn't name a
//                        real user. Deliberately 400, not 404 -- this is a
//                        body-field validation issue, not a resource-path
//                        one (see the E.1 brief).
class BlockBadRequestException : public std::runtime_error
{
  public:
    BlockBadRequestException(std::string code, std::string message)
      : std::runtime_error(std::move(message)), code(std::move(code))
    {
    }

    std::string code;
};

}  // namespace services

#pragma once

#include <stdexcept>
#include <string>

namespace services
{
// Maps to a 403 response: the caller's profile doesn't meet CUJ #3's
// posting gate (verified + minimum photo count). Kept separate from
// ServiceErrors.h since that file only defines 400/404 exception types.
class PostingNotAllowedException : public std::runtime_error
{
  public:
    PostingNotAllowedException(std::string code, std::string message)
      : std::runtime_error(message), code(std::move(code))
    {
    }

    std::string code;
};

}  // namespace services

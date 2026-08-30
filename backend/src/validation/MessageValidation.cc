#include "MessageValidation.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace validation
{
namespace
{
constexpr std::array<const char *, 2> kValidMessageTypes = {"text", "voice"};

bool isBlank(const std::string &s)
{
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); });
}

}  // namespace

bool isValidMessageType(const std::string &type)
{
    for (const auto *valid : kValidMessageTypes)
    {
        if (type == valid)
        {
            return true;
        }
    }
    return false;
}

std::optional<ValidationError> validateMessage(const std::string &type,
                                                const std::string &content)
{
    if (type.empty())
    {
        return ValidationError{"MESSAGE_TYPE_REQUIRED", "type is required."};
    }
    if (!isValidMessageType(type))
    {
        return ValidationError{"MESSAGE_TYPE_INVALID", "type must be 'text' or 'voice'."};
    }
    if (content.empty() || isBlank(content))
    {
        return ValidationError{"MESSAGE_CONTENT_REQUIRED", "content is required."};
    }
    if (content.size() > kMaxMessageContentLength)
    {
        return ValidationError{"MESSAGE_CONTENT_TOO_LONG",
                                "content must be at most " +
                                    std::to_string(kMaxMessageContentLength) + " characters."};
    }
    return std::nullopt;
}

}  // namespace validation

#include "SwipeValidation.h"

#include <array>
#include <cctype>

namespace validation
{
namespace
{
constexpr std::array<const char *, 2> kValidSwipeActions = {"interested", "pass"};

}  // namespace

bool isValidSwipeAction(const std::string &action)
{
    for (const auto *valid : kValidSwipeActions)
    {
        if (action == valid)
        {
            return true;
        }
    }
    return false;
}

bool isLikelyUuid(const std::string &value)
{
    // 8-4-4-4-12 hex with dashes at fixed offsets. This is a syntactic
    // guard only (not a version/variant check) -- enough to keep a
    // garbage path segment from reaching Postgres as a uuid bind param.
    static constexpr size_t kUuidLength = 36;
    if (value.size() != kUuidLength)
    {
        return false;
    }
    for (size_t i = 0; i < kUuidLength; ++i)
    {
        const char c = value[i];
        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (c != '-')
            {
                return false;
            }
        }
        else if (std::isxdigit(static_cast<unsigned char>(c)) == 0)
        {
            return false;
        }
    }
    return true;
}

std::optional<ValidationError> validateSwipeAction(const std::string &action)
{
    if (action.empty())
    {
        return ValidationError{"ACTION_REQUIRED", "action is required."};
    }
    if (!isValidSwipeAction(action))
    {
        return ValidationError{"ACTION_INVALID", "action must be 'interested' or 'pass'."};
    }
    return std::nullopt;
}

}  // namespace validation

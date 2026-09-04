#include "ReportValidation.h"

#include <algorithm>
#include <array>

#include "SwipeValidation.h"  // reused for validation::isLikelyUuid

namespace validation
{
namespace
{
constexpr std::array<const char *, 3> kValidTargetTypes = {
    "profile", "proposal", "conversation"};
constexpr std::array<const char *, 5> kValidReasons = {
    "harassment", "fake_profile", "inappropriate_content", "safety_concern", "other"};

}  // namespace

bool isValidReportTargetType(const std::string &targetType)
{
    return std::find(kValidTargetTypes.begin(), kValidTargetTypes.end(), targetType) !=
           kValidTargetTypes.end();
}

bool isValidReportReason(const std::string &reason)
{
    return std::find(kValidReasons.begin(), kValidReasons.end(), reason) != kValidReasons.end();
}

std::vector<ValidationError> validateReportCreate(const ReportCreateInput &input)
{
    std::vector<ValidationError> errors;

    if (!isValidReportTargetType(input.targetType))
    {
        errors.push_back({"TARGET_TYPE_INVALID",
                           "target_type must be 'profile', 'proposal', or 'conversation'."});
    }

    // A malformed target_id can't name a real row for any target_type --
    // same "guard the uuid shape before it reaches Postgres" pattern
    // SwipeService/PinkyPromiseService use for their own {id} params, but
    // surfaced here as a 400 (TARGET_ID_INVALID) per the E.2 brief, since
    // target_id is a body field here rather than a path segment.
    if (!isLikelyUuid(input.targetId))
    {
        errors.push_back({"TARGET_ID_INVALID", "target_id must be a valid uuid."});
    }

    if (!isValidReportReason(input.reason))
    {
        errors.push_back({"REASON_INVALID",
                           "reason must be one of 'harassment', 'fake_profile', "
                           "'inappropriate_content', 'safety_concern', 'other'."});
    }

    if (input.detailsText.size() > kMaxReportDetailsTextLength)
    {
        errors.push_back({"DETAILS_TEXT_TOO_LONG",
                           "details_text must be at most " +
                               std::to_string(kMaxReportDetailsTextLength) + " characters."});
    }

    return errors;
}

}  // namespace validation

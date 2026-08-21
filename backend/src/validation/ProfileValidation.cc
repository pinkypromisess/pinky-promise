#include "ProfileValidation.h"

#include <algorithm>
#include <array>

namespace validation
{
namespace
{
constexpr std::array<const char *, 4> kValidSexes = {"male", "female", "non_binary", "other"};
constexpr std::array<const char *, 6> kValidRelationshipStatuses = {
    "single", "married", "married_open", "married_separated", "divorced", "widowed"};

bool isBlank(const std::string &s)
{
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); });
}

}  // namespace

bool isValidSex(const std::string &sex)
{
    return std::find(kValidSexes.begin(), kValidSexes.end(), sex) != kValidSexes.end();
}

bool isValidRelationshipStatus(const std::string &status)
{
    return std::find(kValidRelationshipStatuses.begin(),
                      kValidRelationshipStatuses.end(),
                      status) != kValidRelationshipStatuses.end();
}

std::optional<ValidationError> validatePhotoCount(size_t resultingCount)
{
    if (resultingCount < kMinPhotos)
    {
        return ValidationError{
            "PHOTO_MINIMUM_NOT_MET",
            "At least " + std::to_string(kMinPhotos) + " photos are required."};
    }
    return std::nullopt;
}

std::vector<ValidationError> validateProfileUpsert(const ProfileUpsertInput &input)
{
    std::vector<ValidationError> errors;

    if (input.name.empty() || isBlank(input.name))
    {
        errors.push_back({"NAME_REQUIRED", "Name is required."});
    }
    else if (input.name.size() > kMaxNameLength)
    {
        errors.push_back({"NAME_TOO_LONG",
                           "Name must be at most " + std::to_string(kMaxNameLength) +
                               " characters."});
    }

    if (!isValidSex(input.sex))
    {
        errors.push_back({"SEX_INVALID", "Sex must be one of the supported values."});
    }

    if (input.age < kMinAge || input.age > kMaxAge)
    {
        errors.push_back({"AGE_OUT_OF_RANGE",
                           "Age must be between " + std::to_string(kMinAge) + " and " +
                               std::to_string(kMaxAge) + "."});
    }

    if (input.needToKnowText.empty() || isBlank(input.needToKnowText))
    {
        errors.push_back({"NEED_TO_KNOW_REQUIRED",
                           "\"One thing you need to know about me\" is required."});
    }
    else if (input.needToKnowText.size() > kMaxNeedToKnowLength)
    {
        errors.push_back({"NEED_TO_KNOW_TOO_LONG",
                           "Must be at most " + std::to_string(kMaxNeedToKnowLength) +
                               " characters."});
    }

    if (auto photoError = validatePhotoCount(input.photoUrls.size()))
    {
        errors.push_back(*photoError);
    }

    if (input.occupation && input.occupation->size() > kMaxOccupationLength)
    {
        errors.push_back({"OCCUPATION_TOO_LONG",
                           "Occupation must be at most " +
                               std::to_string(kMaxOccupationLength) + " characters."});
    }

    if (input.relationshipStatus && !isValidRelationshipStatus(*input.relationshipStatus))
    {
        errors.push_back(
            {"RELATIONSHIP_STATUS_INVALID",
             "Relationship status must be one of the supported values."});
    }

    return errors;
}

}  // namespace validation

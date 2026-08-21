#include <drogon/drogon_test.h>

#include "../src/validation/ProfileValidation.h"

using namespace validation;

namespace
{
ProfileUpsertInput validInput()
{
    ProfileUpsertInput input;
    input.name = "Alex";
    input.sex = "female";
    input.age = 28;
    input.needToKnowText = "I love hiking.";
    input.photoUrls = {"u1", "u2", "u3", "u4", "u5", "u6"};
    return input;
}

bool hasError(const std::vector<ValidationError> &errors, const std::string &code)
{
    for (const auto &e : errors)
    {
        if (e.code == code)
            return true;
    }
    return false;
}

}  // namespace

// CHECKS: a fully-filled, valid profile produces no validation errors
DROGON_TEST(ValidProfilePassesValidation)
{
    auto errors = validateProfileUpsert(validInput());
    CHECK(errors.empty());
}

// CHECKS: an empty name is rejected as a required field
DROGON_TEST(MissingNameFails)
{
    auto input = validInput();
    input.name = "";
    auto errors = validateProfileUpsert(input);
    CHECK(hasError(errors, "NAME_REQUIRED"));
}

// CHECKS: a whitespace-only name is treated the same as an empty one
DROGON_TEST(BlankNameFails)
{
    auto input = validInput();
    input.name = "   ";
    auto errors = validateProfileUpsert(input);
    CHECK(hasError(errors, "NAME_REQUIRED"));
}

// CHECKS: an empty "need to know" text is rejected as a required field
DROGON_TEST(MissingNeedToKnowTextFails)
{
    auto input = validInput();
    input.needToKnowText = "";
    auto errors = validateProfileUpsert(input);
    CHECK(hasError(errors, "NEED_TO_KNOW_REQUIRED"));
}

// CHECKS: a sex value outside the supported enum is rejected
DROGON_TEST(InvalidSexFails)
{
    auto input = validInput();
    input.sex = "unspecified";
    auto errors = validateProfileUpsert(input);
    CHECK(hasError(errors, "SEX_INVALID"));
}

// CHECKS: age below the 18 minimum is rejected
DROGON_TEST(AgeBelowMinimumFails)
{
    auto input = validInput();
    input.age = 17;
    auto errors = validateProfileUpsert(input);
    CHECK(hasError(errors, "AGE_OUT_OF_RANGE"));
}

// CHECKS: age above the 120 maximum is rejected
DROGON_TEST(AgeAboveMaximumFails)
{
    auto input = validInput();
    input.age = 121;
    auto errors = validateProfileUpsert(input);
    CHECK(hasError(errors, "AGE_OUT_OF_RANGE"));
}

// CHECKS: fewer than the required 6 photos is rejected
DROGON_TEST(FewerThanSixPhotosFails)
{
    auto input = validInput();
    input.photoUrls = {"u1", "u2", "u3", "u4", "u5"};
    auto errors = validateProfileUpsert(input);
    CHECK(hasError(errors, "PHOTO_MINIMUM_NOT_MET"));
}

// CHECKS: exactly 6 photos (the minimum) is accepted
DROGON_TEST(ExactlySixPhotosPasses)
{
    auto input = validInput();
    input.photoUrls = {"u1", "u2", "u3", "u4", "u5", "u6"};
    auto errors = validateProfileUpsert(input);
    CHECK(errors.empty());
}

// CHECKS: more than 6 photos is accepted (6 is a floor, not a cap)
DROGON_TEST(MoreThanSixPhotosPasses)
{
    auto input = validInput();
    input.photoUrls = {"u1", "u2", "u3", "u4", "u5", "u6", "u7"};
    auto errors = validateProfileUpsert(input);
    CHECK(errors.empty());
}

// CHECKS: a relationship_status value outside the supported enum is rejected
DROGON_TEST(InvalidRelationshipStatusFails)
{
    auto input = validInput();
    input.relationshipStatus = "engaged";
    auto errors = validateProfileUpsert(input);
    CHECK(hasError(errors, "RELATIONSHIP_STATUS_INVALID"));
}

// CHECKS: every supported relationship_status enum value is accepted
DROGON_TEST(ValidRelationshipStatusPasses)
{
    auto input = validInput();
    input.relationshipStatus = "married_separated";
    auto errors = validateProfileUpsert(input);
    CHECK(errors.empty());
}

// CHECKS: leaving optional occupation/relationship_status blank is valid, not an error
DROGON_TEST(OccupationAndRelationshipStatusLeftBlankIsValid)
{
    // validInput() leaves both as nullopt (they're optional per CUJ #1).
    auto errors = validateProfileUpsert(validInput());
    CHECK(errors.empty());
}

// CHECKS: validation collects every violation in one pass, not just the first
DROGON_TEST(MultipleViolationsAreAllReported)
{
    ProfileUpsertInput input;  // everything left at its invalid default
    auto errors = validateProfileUpsert(input);
    CHECK(hasError(errors, "NAME_REQUIRED"));
    CHECK(hasError(errors, "SEX_INVALID"));
    CHECK(hasError(errors, "AGE_OUT_OF_RANGE"));
    CHECK(hasError(errors, "NEED_TO_KNOW_REQUIRED"));
    CHECK(hasError(errors, "PHOTO_MINIMUM_NOT_MET"));
}

// CHECKS: the standalone photo-count helper (used by PATCH /profile/photos) enforces the same 6-photo minimum
DROGON_TEST(ValidatePhotoCountHelperMatchesMinimum)
{
    CHECK(!validatePhotoCount(6).has_value());
    CHECK(!validatePhotoCount(10).has_value());
    REQUIRE(validatePhotoCount(5).has_value());
    CHECK(validatePhotoCount(5)->code == "PHOTO_MINIMUM_NOT_MET");
    REQUIRE(validatePhotoCount(0).has_value());
}

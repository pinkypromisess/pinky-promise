#pragma once

#include <optional>
#include <string>
#include <vector>

namespace validation
{
// Mirrors the CHECK constraints in migrations/002_create_profiles.sql —
// kept as named constants here so validation and persistence agree on the
// same numbers.
constexpr int kMinAge = 18;
constexpr int kMaxAge = 120;
constexpr size_t kMinPhotos = 6;
constexpr size_t kMaxNameLength = 100;
constexpr size_t kMaxNeedToKnowLength = 500;
constexpr size_t kMaxOccupationLength = 100;

struct ValidationError
{
    std::string code;
    std::string message;
};

struct ProfileUpsertInput
{
    std::string name;
    std::string sex;
    int age = 0;
    std::string needToKnowText;
    std::vector<std::string> photoUrls;
    std::optional<std::string> occupation;
    std::optional<std::string> relationshipStatus;
};

bool isValidSex(const std::string &sex);
bool isValidRelationshipStatus(const std::string &status);

// Returns every violation found (not just the first), so a client can fix
// all fields in one round trip instead of one error at a time.
std::vector<ValidationError> validateProfileUpsert(const ProfileUpsertInput &input);

// Shared by PATCH /profile/photos: the resulting photo count after
// add/remove is applied must be >= kMinPhotos.
std::optional<ValidationError> validatePhotoCount(size_t resultingCount);

}  // namespace validation

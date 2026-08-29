#pragma once

#include <string>
#include <vector>

#include "ProfileValidation.h"  // reused for validation::ValidationError and kMinPhotos

namespace validation
{
constexpr size_t kMaxActivityTextLength = 300;
constexpr size_t kMaxLookingForTextLength = 300;
constexpr size_t kMaxLocationAddressLength = 300;
constexpr double kMinLatitude = -90.0;
constexpr double kMaxLatitude = 90.0;
constexpr double kMinLongitude = -180.0;
constexpr double kMaxLongitude = 180.0;

struct ProposalLocationInput
{
    double lat = 0.0;
    double lng = 0.0;
    std::string address;
};

struct ProposalCreateInput
{
    std::string activityText;
    std::string eventTime;  // ISO 8601 string, passed through to Postgres as-is
    ProposalLocationInput location;
    std::string paymentType;
    std::string lookingForText;
    std::vector<std::string> revealedFields;  // subset of {"occupation", "relationship_status"}
};

bool isValidPaymentType(const std::string &paymentType);
bool isValidRevealableField(const std::string &field);

// Field-format validation only (required text present, lengths, enum
// membership, lat/lng range, no unknown/duplicate revealed_fields
// entries). Does NOT check revealed_fields against the creator's actual
// profile data (whether occupation/relationship_status are filled in) --
// that's data-dependent and lives in ProposalService, which has the
// loaded profile row.
std::vector<ValidationError> validateProposalCreate(const ProposalCreateInput &input);

}  // namespace validation

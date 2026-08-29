#include "ProposalValidation.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>

namespace validation
{
namespace
{
constexpr std::array<const char *, 4> kValidPaymentTypes = {
    "split", "host_treats", "guest_treats", "tbd"};
constexpr std::array<const char *, 2> kValidRevealableFields = {"occupation",
                                                                  "relationship_status"};

bool isBlank(const std::string &s)
{
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); });
}

}  // namespace

bool isValidPaymentType(const std::string &paymentType)
{
    return std::find(kValidPaymentTypes.begin(), kValidPaymentTypes.end(), paymentType) !=
           kValidPaymentTypes.end();
}

bool isValidRevealableField(const std::string &field)
{
    return std::find(kValidRevealableFields.begin(), kValidRevealableFields.end(), field) !=
           kValidRevealableFields.end();
}

std::vector<ValidationError> validateProposalCreate(const ProposalCreateInput &input)
{
    std::vector<ValidationError> errors;

    if (input.activityText.empty() || isBlank(input.activityText))
    {
        errors.push_back({"ACTIVITY_TEXT_REQUIRED", "Activity text is required."});
    }
    else if (input.activityText.size() > kMaxActivityTextLength)
    {
        errors.push_back({"ACTIVITY_TEXT_TOO_LONG",
                           "Activity text must be at most " +
                               std::to_string(kMaxActivityTextLength) + " characters."});
    }

    if (input.eventTime.empty())
    {
        errors.push_back({"EVENT_TIME_REQUIRED", "Event time is required."});
    }

    if (input.location.address.empty() || isBlank(input.location.address))
    {
        errors.push_back({"LOCATION_ADDRESS_REQUIRED", "Location address is required."});
    }
    else if (input.location.address.size() > kMaxLocationAddressLength)
    {
        errors.push_back({"LOCATION_ADDRESS_TOO_LONG",
                           "Location address must be at most " +
                               std::to_string(kMaxLocationAddressLength) + " characters."});
    }

    if (input.location.lat < kMinLatitude || input.location.lat > kMaxLatitude)
    {
        errors.push_back(
            {"LOCATION_LAT_OUT_OF_RANGE", "Latitude must be between -90 and 90."});
    }

    if (input.location.lng < kMinLongitude || input.location.lng > kMaxLongitude)
    {
        errors.push_back(
            {"LOCATION_LNG_OUT_OF_RANGE", "Longitude must be between -180 and 180."});
    }

    if (!isValidPaymentType(input.paymentType))
    {
        errors.push_back(
            {"PAYMENT_TYPE_INVALID", "Payment type must be one of the supported values."});
    }

    if (input.lookingForText.empty() || isBlank(input.lookingForText))
    {
        errors.push_back({"LOOKING_FOR_TEXT_REQUIRED", "\"Looking for\" text is required."});
    }
    else if (input.lookingForText.size() > kMaxLookingForTextLength)
    {
        errors.push_back({"LOOKING_FOR_TEXT_TOO_LONG",
                           "\"Looking for\" text must be at most " +
                               std::to_string(kMaxLookingForTextLength) + " characters."});
    }

    std::set<std::string> seenRevealedFields;
    for (const auto &field : input.revealedFields)
    {
        if (!isValidRevealableField(field))
        {
            errors.push_back({"REVEALED_FIELDS_INVALID",
                               "revealed_fields may only contain 'occupation' or "
                               "'relationship_status'."});
            break;
        }
        if (!seenRevealedFields.insert(field).second)
        {
            errors.push_back(
                {"REVEALED_FIELDS_DUPLICATE", "revealed_fields must not contain duplicates."});
            break;
        }
    }

    return errors;
}

}  // namespace validation

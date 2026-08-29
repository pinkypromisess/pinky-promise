#include <drogon/drogon_test.h>

#include "../src/validation/ProposalValidation.h"

using namespace validation;

namespace
{
bool hasCode(const std::vector<ValidationError> &errors, const std::string &code)
{
    for (const auto &e : errors)
    {
        if (e.code == code)
        {
            return true;
        }
    }
    return false;
}

ProposalCreateInput makeValidInput()
{
    ProposalCreateInput input;
    input.activityText = "grab coffee";
    input.eventTime = "2026-09-01T18:00:00Z";
    input.location = {37.7749, -122.4194, "123 Main St, San Francisco, CA"};
    input.paymentType = "split";
    input.lookingForText = "someone chill to talk with";
    return input;
}

}  // namespace

// CHECKS: a fully valid ProposalCreateInput produces zero validation errors -- pure logic, no HTTP/DB
DROGON_TEST(ValidProposalInputPassesValidation)
{
    auto errors = validateProposalCreate(makeValidInput());
    CHECK(errors.empty());
}

// CHECKS: an unsupported payment_type value is rejected with PAYMENT_TYPE_INVALID
DROGON_TEST(InvalidPaymentTypeIsRejected)
{
    auto input = makeValidInput();
    input.paymentType = "venmo_request";
    auto errors = validateProposalCreate(input);
    CHECK(hasCode(errors, "PAYMENT_TYPE_INVALID"));
}

// CHECKS: blank activity_text is rejected with ACTIVITY_TEXT_REQUIRED
DROGON_TEST(BlankActivityTextIsRejected)
{
    auto input = makeValidInput();
    input.activityText = "   ";
    auto errors = validateProposalCreate(input);
    CHECK(hasCode(errors, "ACTIVITY_TEXT_REQUIRED"));
}

// CHECKS: blank looking_for_text is rejected with LOOKING_FOR_TEXT_REQUIRED
DROGON_TEST(BlankLookingForTextIsRejected)
{
    auto input = makeValidInput();
    input.lookingForText = "";
    auto errors = validateProposalCreate(input);
    CHECK(hasCode(errors, "LOOKING_FOR_TEXT_REQUIRED"));
}

// CHECKS: a revealed_fields entry outside {occupation, relationship_status} is rejected
DROGON_TEST(UnknownRevealedFieldIsRejected)
{
    auto input = makeValidInput();
    input.revealedFields = {"income"};
    auto errors = validateProposalCreate(input);
    CHECK(hasCode(errors, "REVEALED_FIELDS_INVALID"));
}

// CHECKS: duplicate entries in revealed_fields are rejected
DROGON_TEST(DuplicateRevealedFieldIsRejected)
{
    auto input = makeValidInput();
    input.revealedFields = {"occupation", "occupation"};
    auto errors = validateProposalCreate(input);
    CHECK(hasCode(errors, "REVEALED_FIELDS_DUPLICATE"));
}

// CHECKS: revealed_fields naming both supported values passes format validation
// (whether the creator's profile actually has them filled in is checked
// separately in ProposalService, not here)
DROGON_TEST(BothRevealableFieldsPassFormatValidation)
{
    auto input = makeValidInput();
    input.revealedFields = {"occupation", "relationship_status"};
    auto errors = validateProposalCreate(input);
    CHECK(errors.empty());
}

// CHECKS: latitude outside [-90, 90] is rejected with LOCATION_LAT_OUT_OF_RANGE
DROGON_TEST(OutOfRangeLatitudeIsRejected)
{
    auto input = makeValidInput();
    input.location.lat = 999.0;
    auto errors = validateProposalCreate(input);
    CHECK(hasCode(errors, "LOCATION_LAT_OUT_OF_RANGE"));
}

// CHECKS: longitude outside [-180, 180] is rejected with LOCATION_LNG_OUT_OF_RANGE
DROGON_TEST(OutOfRangeLongitudeIsRejected)
{
    auto input = makeValidInput();
    input.location.lng = -999.0;
    auto errors = validateProposalCreate(input);
    CHECK(hasCode(errors, "LOCATION_LNG_OUT_OF_RANGE"));
}

// CHECKS: a blank location address is rejected with LOCATION_ADDRESS_REQUIRED
DROGON_TEST(BlankLocationAddressIsRejected)
{
    auto input = makeValidInput();
    input.location.address = "";
    auto errors = validateProposalCreate(input);
    CHECK(hasCode(errors, "LOCATION_ADDRESS_REQUIRED"));
}

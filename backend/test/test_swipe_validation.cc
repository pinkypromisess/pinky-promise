#include <drogon/drogon_test.h>

#include "../src/validation/SwipeValidation.h"

using namespace validation;

// All tests in this file are PURE-LOGIC (no HTTP, no DB) -- they exercise
// only validation::SwipeValidation. The HTTP-router + auth-filter + real-DB
// coverage for the swipe endpoint lives in test_http_swipe.cc.

// CHECKS: "interested" and "pass" are the two accepted actions
DROGON_TEST(SwipeActionAcceptsInterestedAndPass)
{
    CHECK(isValidSwipeAction("interested"));
    CHECK(isValidSwipeAction("pass"));
}

// CHECKS: anything else (including case variants and empty) is rejected
DROGON_TEST(SwipeActionRejectsEverythingElse)
{
    CHECK(!isValidSwipeAction(""));
    CHECK(!isValidSwipeAction("Interested"));
    CHECK(!isValidSwipeAction("like"));
    CHECK(!isValidSwipeAction("nope"));
}

// CHECKS: validateSwipeAction returns the right error codes / no error
DROGON_TEST(ValidateSwipeActionErrorCodes)
{
    CHECK(!validateSwipeAction("interested").has_value());
    CHECK(!validateSwipeAction("pass").has_value());

    auto missing = validateSwipeAction("");
    REQUIRE(missing.has_value());
    CHECK(missing->code == "ACTION_REQUIRED");

    auto bad = validateSwipeAction("maybe");
    REQUIRE(bad.has_value());
    CHECK(bad->code == "ACTION_INVALID");
}

// CHECKS: isLikelyUuid accepts a canonical 8-4-4-4-12 hex string
DROGON_TEST(IsLikelyUuidAcceptsCanonicalForm)
{
    CHECK(isLikelyUuid("123e4567-e89b-12d3-a456-426614174000"));
    CHECK(isLikelyUuid("00000000-0000-0000-0000-000000000000"));
}

// CHECKS: isLikelyUuid rejects malformed path segments (wrong length,
// missing dashes, non-hex) -- these become a clean 404 rather than a
// Postgres uuid-cast error
DROGON_TEST(IsLikelyUuidRejectsMalformed)
{
    CHECK(!isLikelyUuid(""));
    CHECK(!isLikelyUuid("nonexistent-id"));
    CHECK(!isLikelyUuid("123e4567e89b12d3a456426614174000"));
    CHECK(!isLikelyUuid("123e4567-e89b-12d3-a456-42661417400"));   // too short
    CHECK(!isLikelyUuid("123e4567-e89b-12d3-a456-426614174000x")); // too long
    CHECK(!isLikelyUuid("zzze4567-e89b-12d3-a456-426614174000"));  // non-hex
}

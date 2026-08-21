#include <drogon/drogon_test.h>

#include "../src/verification/StubFaceVerificationProvider.h"

using namespace verification;

// CHECKS: a default-constructed stub provider resolves a liveness session to Passed with high confidence
DROGON_TEST(DefaultProviderPassesLiveness)
{
    StubFaceVerificationProvider provider;
    auto session = provider.createLivenessSession("user-1");
    CHECK(!session.sessionId.empty());

    auto result = provider.getLivenessResult(session.sessionId);
    REQUIRE(result.status == LivenessStatus::Passed);
    REQUIRE(result.confidence.has_value());
    CHECK(*result.confidence > 90.0);
}

// CHECKS: compareFaces returns a high match score when reference photos are supplied
DROGON_TEST(CompareFacesReturnsHighScoreWithReferencePhotos)
{
    StubFaceVerificationProvider provider;
    auto session = provider.createLivenessSession("user-1");
    auto match = provider.compareFaces(session.sessionId, {"https://example.com/p1.jpg"});
    CHECK(match.bestMatchScore > 80.0);
}

// CHECKS: compareFaces scores zero when there are no reference photos to compare against
DROGON_TEST(CompareFacesWithNoReferencePhotosScoresZero)
{
    StubFaceVerificationProvider provider;
    auto session = provider.createLivenessSession("user-1");
    auto match = provider.compareFaces(session.sessionId, {});
    CHECK(match.bestMatchScore == 0.0);
}

// CHECKS: a provider constructed with a Failed default status reports Failed liveness
DROGON_TEST(ProviderConstructedToFailReturnsFailed)
{
    StubFaceVerificationProvider provider(LivenessStatus::Failed, 40.0, 30.0);
    auto session = provider.createLivenessSession("user-1");
    auto result = provider.getLivenessResult(session.sessionId);
    CHECK(result.status == LivenessStatus::Failed);
}

// CHECKS: setNextOutcome overrides exactly one subsequent call, then reverts to the default outcome
DROGON_TEST(NextOutcomeOverrideAppliesExactlyOnce)
{
    StubFaceVerificationProvider provider;  // defaults to Passed
    auto session = provider.createLivenessSession("user-1");

    provider.setNextOutcome(LivenessStatus::Pending, 0.0);
    auto pendingResult = provider.getLivenessResult(session.sessionId);
    CHECK(pendingResult.status == LivenessStatus::Pending);

    // Override was consumed by the call above; this reverts to the default.
    auto secondResult = provider.getLivenessResult(session.sessionId);
    CHECK(secondResult.status == LivenessStatus::Passed);
}

// CHECKS: each call to createLivenessSession returns a distinct session id
DROGON_TEST(SessionIdsAreUniquePerCall)
{
    StubFaceVerificationProvider provider;
    auto s1 = provider.createLivenessSession("user-1");
    auto s2 = provider.createLivenessSession("user-1");
    CHECK(s1.sessionId != s2.sessionId);
}

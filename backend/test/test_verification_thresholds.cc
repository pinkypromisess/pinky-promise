#include <drogon/drogon_test.h>

#include "TestDb.h"
#include "TestHttpFixtures.h"
#include "TestHttpServer.h"
#include "../src/services/VerificationService.h"

// Confirms VerificationService::kLivenessConfidenceThreshold and
// kFaceMatchThreshold actually gate the pass/fail decision, rather than
// being unused numbers — every case pins one axis comfortably inside its
// threshold and walks the other axis across the boundary. Values are
// computed from the real constants (threshold - 1 / threshold), not
// hardcoded as 89/90/79/80, so this doesn't silently stop testing the
// boundary if the thresholds are ever tuned.
//
// Drives POST /verification + GET /verification/status over real HTTP
// against the actually-running server; the stub provider's *next* outcome
// is reconfigured per test via testServerVerificationProvider() so the
// boundary values reach the real VerificationService::getStatus decision
// logic exactly the way a real vendor response would.

using namespace test_support;
using namespace drogon;
using namespace services;
using namespace verification;

namespace
{
std::string runVerificationOverHttp(const TestSession &s, double livenessConfidence, double faceMatchScore)
{
    auto startResp = sendTestRequest(s.baseUrl, Post, "/v1/verification", s.token);
    if (startResp.status != k201Created)
    {
        throw std::runtime_error("POST /v1/verification returned unexpected status " +
                                  std::to_string(startResp.status));
    }

    auto provider = testServerVerificationProvider();
    provider->setNextOutcome(LivenessStatus::Passed, livenessConfidence);
    provider->setNextFaceMatchScore(faceMatchScore);

    auto statusResp = sendTestRequest(s.baseUrl, Get, "/v1/verification/status", s.token);
    if (statusResp.status != k200OK)
    {
        throw std::runtime_error("GET /v1/verification/status returned unexpected status " +
                                  std::to_string(statusResp.status));
    }
    return statusResp.json["decision"].asString();
}

}  // namespace

// CHECKS: liveness confidence one point below the threshold (with face match comfortably passing) results in decision=fail
DROGON_TEST(LivenessJustBelowThresholdFails)
{
    try
    {
        auto s = setUpTestSession();
        putProfileOverHttp(s, sixPhotoUrls("t"));
        const auto decision = runVerificationOverHttp(
            s, VerificationService::kLivenessConfidenceThreshold - 1.0,
            VerificationService::kFaceMatchThreshold + 10.0);
        CHECK(decision == "fail");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: liveness confidence exactly at the threshold (with face match comfortably passing) results in decision=pass
DROGON_TEST(LivenessAtThresholdPasses)
{
    try
    {
        auto s = setUpTestSession();
        putProfileOverHttp(s, sixPhotoUrls("t"));
        const auto decision = runVerificationOverHttp(
            s, VerificationService::kLivenessConfidenceThreshold,
            VerificationService::kFaceMatchThreshold + 10.0);
        CHECK(decision == "pass");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: face-match score one point below the threshold (with liveness comfortably passing) results in decision=fail
DROGON_TEST(FaceMatchJustBelowThresholdFails)
{
    try
    {
        auto s = setUpTestSession();
        putProfileOverHttp(s, sixPhotoUrls("t"));
        const auto decision = runVerificationOverHttp(
            s, VerificationService::kLivenessConfidenceThreshold + 5.0,
            VerificationService::kFaceMatchThreshold - 1.0);
        CHECK(decision == "fail");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: face-match score exactly at the threshold (with liveness comfortably passing) results in decision=pass
DROGON_TEST(FaceMatchAtThresholdPasses)
{
    try
    {
        auto s = setUpTestSession();
        putProfileOverHttp(s, sixPhotoUrls("t"));
        const auto decision = runVerificationOverHttp(
            s, VerificationService::kLivenessConfidenceThreshold + 5.0,
            VerificationService::kFaceMatchThreshold);
        CHECK(decision == "pass");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

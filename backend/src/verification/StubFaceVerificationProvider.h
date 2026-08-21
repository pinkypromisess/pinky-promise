#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>

#include "FaceVerificationProvider.h"

namespace verification
{
// Deterministic fake vendor implementation. Used for local development and
// tests so the app runs end-to-end without AWS credentials. Every session
// resolves to `defaultStatus` unless overridden per-call via
// setNextOutcome()/setNextFaceMatchScore().
//
// When this instance is shared with a running Drogon app under test (see
// backend/test/TestHttpServer.h), the overrides are set on the test's
// calling thread before it issues an HTTP request, and consumed on the
// app's event-loop thread while handling that request — genuinely two
// different OS threads — so the override state is mutex-guarded rather
// than relying on incidental ordering from the network round trip.
class StubFaceVerificationProvider : public FaceVerificationProvider
{
  public:
    explicit StubFaceVerificationProvider(LivenessStatus defaultStatus = LivenessStatus::Passed,
                                           double defaultConfidence = 99.0,
                                           double defaultFaceMatchScore = 95.0);

    LivenessSession createLivenessSession(const std::string &userId) override;
    LivenessResult getLivenessResult(const std::string &sessionId) override;
    FaceMatchResult compareFaces(const std::string &sessionId,
                                  const std::vector<std::string> &referencePhotoUrls) override;

    // Test hook: the next call to getLivenessResult() (for any session)
    // returns this outcome once, then reverts to the default.
    void setNextOutcome(LivenessStatus status, double confidence);

    // Test hook: the next call to compareFaces() (for any session, given
    // at least one reference photo) returns this score once, then reverts
    // to the default.
    void setNextFaceMatchScore(double score);

  private:
    LivenessStatus defaultStatus_;
    double defaultConfidence_;
    double defaultFaceMatchScore_;

    std::mutex mutex_;
    std::optional<std::pair<LivenessStatus, double>> nextOutcomeOverride_;
    std::optional<double> nextFaceMatchScoreOverride_;

    std::atomic<uint64_t> sessionCounter_{0};
};

}  // namespace verification

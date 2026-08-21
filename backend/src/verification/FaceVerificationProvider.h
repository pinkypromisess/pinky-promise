#pragma once

#include <optional>
#include <string>
#include <vector>

namespace verification
{
enum class LivenessStatus
{
    Pending,
    Passed,
    Failed
};

struct LivenessSession
{
    std::string sessionId;
};

struct LivenessResult
{
    LivenessStatus status = LivenessStatus::Pending;
    // 0-100. Empty while status == Pending.
    std::optional<double> confidence;
};

struct FaceMatchResult
{
    // Highest similarity across the supplied reference photos, 0-100.
    double bestMatchScore = 0.0;
};

// Abstracts the identity-verification vendor (AWS Rekognition Face Liveness
// + CompareFaces per docs/pinky-promise-cujs.md CUJ #2) behind an interface
// so VerificationService never talks to the vendor SDK directly, and a real
// implementation can be swapped in later without touching call sites.
//
// Only StubFaceVerificationProvider is implemented for now — no AWS
// credentials are wired up yet.
class FaceVerificationProvider
{
  public:
    virtual ~FaceVerificationProvider() = default;

    // Starts a liveness session for `userId`. The mobile client performs
    // the actual live capture directly against the vendor's client SDK
    // using the returned session id — the capture never transits this
    // backend.
    virtual LivenessSession createLivenessSession(const std::string &userId) = 0;

    // Fetches the outcome of a previously created liveness session.
    // Returns LivenessStatus::Pending if the client hasn't finished the
    // capture yet.
    virtual LivenessResult getLivenessResult(const std::string &sessionId) = 0;

    // Compares the completed liveness session's captured frame against the
    // given reference photo URLs (the user's current profile photos),
    // returning the best match score. Only valid to call once
    // getLivenessResult() reports Passed.
    virtual FaceMatchResult compareFaces(const std::string &sessionId,
                                          const std::vector<std::string> &referencePhotoUrls) = 0;
};

}  // namespace verification

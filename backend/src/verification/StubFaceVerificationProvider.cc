#include "StubFaceVerificationProvider.h"

namespace verification
{
StubFaceVerificationProvider::StubFaceVerificationProvider(LivenessStatus defaultStatus,
                                                             double defaultConfidence,
                                                             double defaultFaceMatchScore)
  : defaultStatus_(defaultStatus),
    defaultConfidence_(defaultConfidence),
    defaultFaceMatchScore_(defaultFaceMatchScore)
{
}

LivenessSession StubFaceVerificationProvider::createLivenessSession(const std::string &userId)
{
    (void)userId;
    auto id = sessionCounter_.fetch_add(1, std::memory_order_relaxed);
    return LivenessSession{"stub-session-" + std::to_string(id)};
}

LivenessResult StubFaceVerificationProvider::getLivenessResult(const std::string &sessionId)
{
    (void)sessionId;
    std::lock_guard<std::mutex> lock(mutex_);
    if (nextOutcomeOverride_)
    {
        LivenessResult result{nextOutcomeOverride_->first, nextOutcomeOverride_->second};
        nextOutcomeOverride_.reset();
        return result;
    }
    if (defaultStatus_ == LivenessStatus::Pending)
    {
        return LivenessResult{LivenessStatus::Pending, std::nullopt};
    }
    return LivenessResult{defaultStatus_, defaultConfidence_};
}

FaceMatchResult StubFaceVerificationProvider::compareFaces(
    const std::string &sessionId, const std::vector<std::string> &referencePhotoUrls)
{
    (void)sessionId;
    if (referencePhotoUrls.empty())
    {
        return FaceMatchResult{0.0};
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (nextFaceMatchScoreOverride_)
    {
        FaceMatchResult result{*nextFaceMatchScoreOverride_};
        nextFaceMatchScoreOverride_.reset();
        return result;
    }
    return FaceMatchResult{defaultFaceMatchScore_};
}

void StubFaceVerificationProvider::setNextOutcome(LivenessStatus status, double confidence)
{
    std::lock_guard<std::mutex> lock(mutex_);
    nextOutcomeOverride_ = {status, confidence};
}

void StubFaceVerificationProvider::setNextFaceMatchScore(double score)
{
    std::lock_guard<std::mutex> lock(mutex_);
    nextFaceMatchScoreOverride_ = score;
}

}  // namespace verification

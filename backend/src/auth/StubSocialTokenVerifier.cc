#include "StubSocialTokenVerifier.h"

namespace auth
{
std::optional<VerifiedSocialIdentity> StubSocialTokenVerifier::verify(const std::string &idToken)
{
    (void)idToken;
    std::lock_guard<std::mutex> lock(mutex_);
    return nextIdentity_;
}

void StubSocialTokenVerifier::setNextIdentity(VerifiedSocialIdentity identity)
{
    std::lock_guard<std::mutex> lock(mutex_);
    nextIdentity_ = std::move(identity);
}

void StubSocialTokenVerifier::setNextFailure()
{
    std::lock_guard<std::mutex> lock(mutex_);
    nextIdentity_ = std::nullopt;
}

}  // namespace auth

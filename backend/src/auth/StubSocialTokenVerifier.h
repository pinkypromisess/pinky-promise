#pragma once

#include <mutex>
#include <optional>
#include <string>

#include "SocialTokenVerifier.h"

namespace auth
{
// Deterministic fake: returns a fixed, test-configured outcome instead of
// contacting any real provider/JWKS endpoint. Used for local development
// and tests -- same idiom as verification::StubFaceVerificationProvider /
// notifications::StubReminderProvider. One instance per provider (Google,
// Apple) is wired into the test server, same as those.
//
// Defaults to "fail" (verify() returns std::nullopt) until a test calls
// setNextIdentity() -- a safer default than silently succeeding for an
// unconfigured test.
class StubSocialTokenVerifier : public SocialTokenVerifier
{
  public:
    std::optional<VerifiedSocialIdentity> verify(const std::string &idToken) override;

    // Test hook: verify() (for any token) returns this identity from now
    // on, until changed again. Unlike StubFaceVerificationProvider's
    // one-shot overrides, this persists -- simpler here since a test sets
    // up exactly the outcome it wants and there's no pending/resolved
    // state machine to model for a single synchronous call.
    void setNextIdentity(VerifiedSocialIdentity identity);

    // Test hook: verify() (for any token) returns std::nullopt from now
    // on, simulating a bad signature / expired / wrong-audience token --
    // callers don't need to distinguish which, per SocialTokenVerifier's
    // own contract.
    void setNextFailure();

  private:
    mutable std::mutex mutex_;
    std::optional<VerifiedSocialIdentity> nextIdentity_;
};

}  // namespace auth

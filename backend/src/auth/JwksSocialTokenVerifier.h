#pragma once

#include <trantor/net/EventLoopThread.h>

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "SocialTokenVerifier.h"

namespace auth
{
// Real implementation: verifies an RS256 OIDC id token against a live
// JWKS endpoint. Signature verification, JWK-to-key conversion, and
// algorithm-confusion guarding are all handled by jwt-cpp (see .cc) --
// not hand-rolled against raw OpenSSL primitives. `iss` (checked against
// a fixed set of acceptable issuers -- Google alone has used two
// historically) and `aud` (checked against the live value of
// `audienceEnvVar`, read via getenv on every verify() call) are checked
// on top of what jwt-cpp verifies natively (signature, algorithm, `exp`).
//
// One instance is configured per provider -- see GoogleTokenVerifier.cc /
// AppleTokenVerifier.cc for Google's/Apple's fixed jwksUrl/issuers/env
// var name; this class itself has no Google/Apple-specific knowledge.
//
// If `audienceEnvVar` isn't set in the environment, verify() fails closed
// (returns std::nullopt) rather than skipping the audience check or
// crashing -- there is no safe insecure fallback for an OAuth client id,
// unlike JWT_SECRET's local-dev fallback in auth::signingSecret().
class JwksSocialTokenVerifier : public SocialTokenVerifier
{
  public:
    JwksSocialTokenVerifier(std::string providerLabel,
                             std::string jwksUrl,
                             std::vector<std::string> acceptableIssuers,
                             std::string audienceEnvVar);

    std::optional<VerifiedSocialIdentity> verify(const std::string &idToken) override;

  private:
    std::string providerLabel_;
    std::string jwksUrl_;
    std::vector<std::string> acceptableIssuers_;
    std::string audienceEnvVar_;

    // Outbound JWKS fetches run on this dedicated background loop, not the
    // app's own IO loops -- same reasoning as
    // storage::GcsSignBlobUploadUrlProvider::ioThread_: verify() blocks
    // the calling thread on a promise/future while the network work and
    // its response callback run entirely on this loop, so there's no risk
    // of a thread waiting on a callback that needs that same thread to
    // run (the exact deadlock this pattern avoids).
    trantor::EventLoopThread ioThread_;

    std::mutex jwksMutex_;
    std::string cachedJwksBody_;  // raw JSON body; empty if never fetched.

    // Always performs a fresh blocking HTTP GET and updates the cache.
    // Throws on any transport/non-200 failure.
    std::string fetchAndCacheJwks();
};

}  // namespace auth

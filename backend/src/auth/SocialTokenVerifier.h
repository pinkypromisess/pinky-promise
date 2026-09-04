#pragma once

#include <optional>
#include <string>

namespace auth
{
// The provider-vouched identity extracted from a verified Google/Apple id
// token: `subject` is the provider's stable per-user id (the token's `sub`
// claim), `email` is the address the provider vouches for at signup time
// (the token's `email` claim).
struct VerifiedSocialIdentity
{
    std::string subject;
    std::string email;
};

// Abstracts "verify a provider-issued OIDC id token" behind an interface,
// same pattern as verification::FaceVerificationProvider /
// notifications::ReminderProvider / storage::GcsUploadUrlProvider -- so
// AuthService never talks to jwt-cpp or a provider's JWKS endpoint
// directly, and tests can inject a deterministic stub (see
// StubSocialTokenVerifier).
class SocialTokenVerifier
{
  public:
    virtual ~SocialTokenVerifier() = default;

    // Verifies `idToken`'s signature (against the provider's live JWKS),
    // issuer, audience, and expiry. Returns the identity it vouches for on
    // success, or std::nullopt on ANY failure (bad signature, expired,
    // wrong issuer/audience, malformed token, wrong/unsupported algorithm,
    // JWKS unreachable, or the provider's configured audience env var
    // missing) -- deliberately not distinguishing failure subtypes to the
    // caller, same "don't leak detail" spirit as Module F.1's login error.
    virtual std::optional<VerifiedSocialIdentity> verify(const std::string &idToken) = 0;
};

}  // namespace auth

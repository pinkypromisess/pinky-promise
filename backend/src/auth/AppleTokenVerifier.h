#pragma once

#include <memory>

#include "SocialTokenVerifier.h"

namespace auth
{
// Builds a JwksSocialTokenVerifier configured with Apple's fixed JWKS
// URL, acceptable issuer, and audience env var name -- see
// https://developer.apple.com/documentation/sign_in_with_apple/verifying_a_user.
// Audience is checked against the APPLE_SERVICES_ID env var (the Services
// ID / client id configured for Sign in with Apple, which is what Apple
// puts in `aud`, not the app's bundle id).
std::shared_ptr<SocialTokenVerifier> makeAppleTokenVerifier();

}  // namespace auth

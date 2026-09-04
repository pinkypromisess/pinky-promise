#pragma once

#include <memory>

#include "SocialTokenVerifier.h"

namespace auth
{
// Builds a JwksSocialTokenVerifier configured with Google's fixed JWKS
// URL, acceptable issuers, and audience env var name -- see
// https://developers.google.com/identity/openid-connect/openid-connect.
// Google has historically issued id tokens with either
// "https://accounts.google.com" or "accounts.google.com" as `iss`, so
// both are accepted. Audience is checked against the GOOGLE_OAUTH_CLIENT_ID
// env var.
std::shared_ptr<SocialTokenVerifier> makeGoogleTokenVerifier();

}  // namespace auth

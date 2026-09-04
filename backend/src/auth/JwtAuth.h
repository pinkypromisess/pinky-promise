#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace auth
{
// Verifies an HS256-signed JWT and extracts the `sub` claim as the caller's
// user id, per openapi.yaml's bearerAuth scheme.
//
// Returns the user id on success, nullopt if the token is missing,
// malformed, expired (`exp` in the past), or fails signature verification.
std::optional<std::string> verifyAndExtractUserId(const std::string &bearerToken,
                                                    const std::string &hmacSecret);

// Signs an HS256 JWT with `userId` as the `sub` claim and an `exp` claim
// `ttlSeconds` from now, in the exact format verifyAndExtractUserId()
// above expects (built from the same base64url/HMAC-SHA256 building
// blocks, not a second parallel implementation). Added for Module F.1
// (email+password signup/login) -- token *issuance* now lives here,
// alongside the verification this file already had.
std::string signJwt(const std::string &userId, const std::string &hmacSecret,
                     int64_t ttlSeconds);

// The HMAC secret used for both signing (signJwt) and verifying
// (verifyAndExtractUserId, via AuthFilter) tokens: the JWT_SECRET env var,
// falling back to a fixed local-dev-only value so the server/tests are
// runnable without extra setup. Cloud Run deployments must set JWT_SECRET.
// Extracted from AuthFilter.cc (which previously had its own private,
// unexported copy of this) so both paths read the identical secret.
std::string signingSecret();

}  // namespace auth

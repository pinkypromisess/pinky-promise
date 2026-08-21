#pragma once

#include <optional>
#include <string>

namespace auth
{
// Verifies an HS256-signed JWT and extracts the `sub` claim as the caller's
// user id, per openapi.yaml's bearerAuth scheme. Token *issuance* (login/
// signup) belongs to whichever module owns the User entity's auth flow —
// this module only needs to verify a token it's handed and identify the
// caller, so that's out of scope here.
//
// Returns the user id on success, nullopt if the token is missing,
// malformed, expired (`exp` in the past), or fails signature verification.
std::optional<std::string> verifyAndExtractUserId(const std::string &bearerToken,
                                                    const std::string &hmacSecret);

}  // namespace auth

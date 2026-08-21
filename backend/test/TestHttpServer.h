#pragma once

#include <drogon/HttpTypes.h>
#include <json/json.h>

#include <memory>
#include <string>

#include "../src/verification/StubFaceVerificationProvider.h"

// Boots the real Drogon app — actual registered routes (ProfileController,
// VerificationController) behind the actual auth::AuthFilter — so a test
// can hit an endpoint the way a real client would, instead of calling the
// controller/service C++ API directly.
namespace test_support
{
// Starts the server on a background thread the first time it's called
// (subsequent calls are a no-op) and blocks until it's accepting
// connections. Returns the base URL. Throws if the test database isn't
// reachable — callers should wrap this in try/catch and FAIL() rather
// than let the exception escape uncaught.
std::string ensureTestServerRunning();

// The StubFaceVerificationProvider instance actually wired into the
// running test server (same one ensureTestServerRunning() constructed).
// Use its setNextOutcome()/setNextFaceMatchScore() to control the next
// POST /verification + GET /verification/status round trip's decision
// from a test. Only valid after ensureTestServerRunning() has been called
// at least once.
std::shared_ptr<verification::StubFaceVerificationProvider> testServerVerificationProvider();

// Signs an HS256 JWT with `userId` as the `sub` claim, using the same
// secret auth::AuthFilter falls back to when JWT_SECRET isn't set
// ("local-dev-insecure-secret") — fine for talking to the test server,
// never meant to represent anything real.
std::string signTestJwt(const std::string &userId);

struct HttpTestResponse
{
    drogon::HttpStatusCode status = drogon::kUnknown;
    // Empty/null if the body was absent or not valid JSON.
    Json::Value json;
};

// Sends a real HTTP request to the (already-running) test server and
// blocks until the response arrives. `jsonBody` is optional (pass nullptr
// for GET-with-no-body). Throws if the request itself fails at the
// transport level (connection refused, timeout, etc.) — a non-2xx HTTP
// response is not a throw, it's a normal HttpTestResponse with that
// status code, since asserting on error statuses (401, 400, ...) is
// exactly what several of these tests do.
HttpTestResponse sendTestRequest(const std::string &baseUrl,
                                  drogon::HttpMethod method,
                                  const std::string &path,
                                  const std::string &bearerToken,
                                  const Json::Value *jsonBody = nullptr);

}  // namespace test_support

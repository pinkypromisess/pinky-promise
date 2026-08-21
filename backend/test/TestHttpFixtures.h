#pragma once

#include <json/json.h>

#include <string>
#include <vector>

// Shared fixtures for tests that drive the real running server over HTTP
// (see TestHttpServer.h) rather than calling ProfileService/
// VerificationService directly.
namespace test_support
{
struct TestSession
{
    std::string baseUrl;
    std::string token;
    std::string userId;
};

// Creates a fresh `users` row (the only unavoidable non-HTTP step — user
// signup/token issuance isn't part of this module's API surface) and
// starts/reuses the test server. Throws if the DB or server aren't
// reachable.
TestSession setUpTestSession();

Json::Value jsonStringArray(const std::vector<std::string> &values);

// PUT /v1/profile with the given photo URLs (all other fields fixed to a
// valid default). Throws (not FAIL/CHECK — see the .cc for why) if the
// response isn't 200.
Json::Value putProfileOverHttp(const TestSession &session, const std::vector<std::string> &photoUrls);

// Drives POST /v1/verification + GET /v1/verification/status and returns
// the resulting decision ("pass"/"fail"). Throws if either call doesn't
// return the expected status.
std::string verifyOverHttp(const TestSession &session);

// GET /v1/profile/me and return just `verified`. Throws if the response
// isn't 200.
bool getProfileVerifiedOverHttp(const TestSession &session);

}  // namespace test_support

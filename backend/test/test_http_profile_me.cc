#include <drogon/drogon_test.h>

#include "TestDb.h"
#include "TestHttpFixtures.h"
#include "TestHttpServer.h"

using namespace test_support;
using namespace drogon;

// CHECKS: GET /v1/profile/me, sent as a real HTTP request against the actually-running server (real routing, real auth::AuthFilter, real DB), returns the caller's current profile — this endpoint previously had no test of any kind
DROGON_TEST(GetProfileMeReturnsCurrentProfileOverHttp)
{
    try
    {
        auto s = setUpTestSession();
        putProfileOverHttp(s, sixPhotoUrls("http"));

        auto resp = sendTestRequest(s.baseUrl, Get, "/v1/profile/me", s.token);
        REQUIRE(resp.status == k200OK);
        CHECK(resp.json["user_id"].asString() == s.userId);
        CHECK(resp.json["name"].asString() == "Test User");
        CHECK(resp.json["photos"].size() == 6u);
        CHECK(resp.json["verified"].asBool() == false);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: GET /v1/profile/me without a bearer token is rejected with 401, never reaching the controller
DROGON_TEST(GetProfileMeWithoutAuthHeaderReturns401)
{
    try
    {
        auto baseUrl = ensureTestServerRunning();
        auto resp = sendTestRequest(baseUrl, Get, "/v1/profile/me", /*bearerToken=*/"");
        CHECK(resp.status == k401Unauthorized);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

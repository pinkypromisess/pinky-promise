#include <drogon/drogon_test.h>

#include "TestDb.h"
#include "TestHttpFixtures.h"
#include "TestHttpServer.h"

using namespace test_support;
using namespace drogon;

// CHECKS (all HTTP-level: real router, real auth::AuthFilter, real
// Postgres via TestHttpServer): GET /v1/profile/{user_id} — the bare
// profile view added so Frontend Module 3 (Conversations) can show who a
// conversation participant actually is (CUJ #4: "A can view B's profile —
// fields 1,2,3,4,5 only"). Two independent sessions stand in for A and B.

DROGON_TEST(GetUserProfileReturnsBareViewOfAnotherUser)
{
    try
    {
        auto viewer = setUpTestSession();
        auto target = setUpTestSession();
        putProfileOverHttp(target, sixPhotoUrls("http://bare-view-target"));

        auto resp = sendTestRequest(
            viewer.baseUrl, Get, "/v1/profile/" + target.userId, viewer.token);
        REQUIRE(resp.status == k200OK);
        CHECK(resp.json["user_id"].asString() == target.userId);
        CHECK(resp.json["name"].asString() == "Test User");
        CHECK(resp.json["sex"].asString() == "female");
        CHECK(resp.json["age"].asInt() == 30);
        CHECK(resp.json["need_to_know_text"].asString() == "I love testing.");
        CHECK(resp.json["photos"].size() == 6u);

        // Fields 6/7 (and verified/created_at) must NOT leak into a bare view.
        CHECK(resp.json.isMember("occupation") == false);
        CHECK(resp.json.isMember("relationship_status") == false);
        CHECK(resp.json.isMember("verified") == false);
        CHECK(resp.json.isMember("created_at") == false);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

DROGON_TEST(GetUserProfileCanViewOwnProfileToo)
{
    try
    {
        auto s = setUpTestSession();
        putProfileOverHttp(s, sixPhotoUrls("http://bare-view-self"));

        auto resp = sendTestRequest(s.baseUrl, Get, "/v1/profile/" + s.userId, s.token);
        REQUIRE(resp.status == k200OK);
        CHECK(resp.json["user_id"].asString() == s.userId);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

DROGON_TEST(GetUserProfileUnknownOrMalformedIdReturns404)
{
    try
    {
        auto s = setUpTestSession();

        auto unknown = sendTestRequest(
            s.baseUrl, Get, "/v1/profile/00000000-0000-0000-0000-000000000000", s.token);
        CHECK(unknown.status == k404NotFound);

        auto malformed = sendTestRequest(s.baseUrl, Get, "/v1/profile/not-a-uuid", s.token);
        CHECK(malformed.status == k404NotFound);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

DROGON_TEST(GetUserProfileNoProfileYetReturns404)
{
    try
    {
        auto viewer = setUpTestSession();
        auto target = setUpTestSession();  // user row exists, no profile created

        auto resp = sendTestRequest(
            viewer.baseUrl, Get, "/v1/profile/" + target.userId, viewer.token);
        CHECK(resp.status == k404NotFound);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

DROGON_TEST(GetUserProfileWithoutAuthHeaderReturns401)
{
    try
    {
        auto s = setUpTestSession();
        auto resp = sendTestRequest(
            s.baseUrl, Get, "/v1/profile/" + s.userId, /*bearerToken=*/"");
        CHECK(resp.status == k401Unauthorized);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// Regression guard: adding a GET /v1/profile/{user_id} route must not
// shadow the existing static GET /v1/profile/me route (Drogon should
// prefer the exact-match static path over the parameterized one, but this
// pins the behavior with a real request rather than trusting that).
DROGON_TEST(GetProfileMeStillWorksAlongsideTheNewParameterizedRoute)
{
    try
    {
        auto s = setUpTestSession();
        putProfileOverHttp(s, sixPhotoUrls("http://me-vs-param"));

        auto resp = sendTestRequest(s.baseUrl, Get, "/v1/profile/me", s.token);
        REQUIRE(resp.status == k200OK);
        CHECK(resp.json["user_id"].asString() == s.userId);
        // The full-profile fields that the bare view deliberately omits
        // must still be present on /profile/me.
        CHECK(resp.json.isMember("verified"));
        CHECK(resp.json.isMember("created_at"));
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

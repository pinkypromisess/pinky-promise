#include <drogon/drogon_test.h>

#include "TestHttpFixtures.h"
#include "TestHttpServer.h"

// POST /profile/photos/upload-url, driven over real HTTP against the
// actually-running server (real routing, real auth::AuthFilter) — per the
// module's standing requirement, not by calling PhotoUploadService
// directly. The stub GCS provider wired into the test server (see
// TestHttpServer.h) performs no real signing/network I/O; these tests
// verify this endpoint's own logic (auth, content-type validation, the
// server-derived object path), not GCS's actual behavior — the real
// signer (GcsSignBlobUploadUrlProvider) needs a live GCP environment
// nothing in this repo has, so its deterministic parts are covered
// separately in test_gcs_v4_signing.cc instead.

using namespace test_support;
using namespace drogon;

namespace
{
Json::Value contentTypeBody(const std::string &contentType)
{
    Json::Value body;
    body["content_type"] = contentType;
    return body;
}

}  // namespace

// CHECKS: a supported content_type returns 200 with upload_url/object_url/expires_at, and object_url embeds the caller's own user_id with the correct extension
DROGON_TEST(PostPhotoUploadUrlReturnsSignedUrlShapeOverHttp)
{
    try
    {
        auto s = setUpTestSession();
        auto body = contentTypeBody("image/jpeg");
        auto resp = sendTestRequest(s.baseUrl, Post, "/v1/profile/photos/upload-url", s.token, &body);

        REQUIRE(resp.status == k200OK);
        const auto uploadUrl = resp.json["upload_url"].asString();
        const auto objectUrl = resp.json["object_url"].asString();
        const auto expiresAt = resp.json["expires_at"].asString();

        CHECK(!uploadUrl.empty());
        CHECK(!expiresAt.empty());
        CHECK(objectUrl.find("profile-photos/" + s.userId + "/") != std::string::npos);
        CHECK(objectUrl.size() >= 4);
        CHECK(objectUrl.substr(objectUrl.size() - 4) == ".jpg");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: an unsupported content_type is rejected with 400, not silently accepted
DROGON_TEST(PostPhotoUploadUrlWithUnsupportedContentTypeReturns400)
{
    try
    {
        auto s = setUpTestSession();
        auto body = contentTypeBody("application/pdf");
        auto resp = sendTestRequest(s.baseUrl, Post, "/v1/profile/photos/upload-url", s.token, &body);
        CHECK(resp.status == k400BadRequest);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: the endpoint requires auth like every other Profile route — no bearer token, no URL
DROGON_TEST(PostPhotoUploadUrlWithoutAuthReturns401)
{
    try
    {
        auto baseUrl = ensureTestServerRunning();
        auto body = contentTypeBody("image/jpeg");
        auto resp =
            sendTestRequest(baseUrl, Post, "/v1/profile/photos/upload-url", /*bearerToken=*/"", &body);
        CHECK(resp.status == k401Unauthorized);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: two calls for the same user generate two distinct object paths (a fresh uuid each time), not a reused/predictable one
DROGON_TEST(PostPhotoUploadUrlGeneratesDistinctObjectPathsPerCall)
{
    try
    {
        auto s = setUpTestSession();
        auto body = contentTypeBody("image/png");

        auto first = sendTestRequest(s.baseUrl, Post, "/v1/profile/photos/upload-url", s.token, &body);
        auto second = sendTestRequest(s.baseUrl, Post, "/v1/profile/photos/upload-url", s.token, &body);

        REQUIRE(first.status == k200OK);
        REQUIRE(second.status == k200OK);
        CHECK(first.json["object_url"].asString() != second.json["object_url"].asString());
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

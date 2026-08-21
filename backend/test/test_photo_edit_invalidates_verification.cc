#include <drogon/drogon_test.h>

#include "TestDb.h"
#include "TestHttpFixtures.h"
#include "TestHttpServer.h"

// The single highest-value gap called out in the coverage review: CUJ #7
// says "any photo change invalidates verification". This drives the real,
// running server over real HTTP — PUT /profile, PATCH /profile/photos,
// POST /verification, GET /verification/status, GET /profile/me — through
// actual routing and auth::AuthFilter, not by calling ProfileService /
// VerificationService C++ methods directly.

using namespace test_support;
using namespace drogon;

// CHECKS: PUT /profile with a photo_urls list that differs from what's stored resets verified to false, even though it was true beforehand — asserted over real HTTP end to end
DROGON_TEST(PutProfileWithChangedPhotosInvalidatesVerification)
{
    try
    {
        auto s = setUpTestSession();

        auto photos = sixPhotoUrls("orig");
        auto created = putProfileOverHttp(s, photos);
        REQUIRE(!created["verified"].asBool());

        REQUIRE(verifyOverHttp(s) == "pass");
        REQUIRE(getProfileVerifiedOverHttp(s));

        // Same count, but the first URL changed — this must be treated as
        // a photo change, not a no-op.
        auto changedPhotos = photos;
        changedPhotos[0] = "orig1-replaced";
        auto updated = putProfileOverHttp(s, changedPhotos);
        CHECK(!updated["verified"].asBool());

        // Not just the response body — the persisted row too, fetched via
        // a separate request.
        CHECK(!getProfileVerifiedOverHttp(s));
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: PATCH /profile/photos (add+remove) resets verified to false, even though it was true beforehand — asserted over real HTTP end to end
DROGON_TEST(PatchProfilePhotosInvalidatesVerification)
{
    try
    {
        auto s = setUpTestSession();

        auto photos = sixPhotoUrls("p");
        auto created = putProfileOverHttp(s, photos);

        REQUIRE(verifyOverHttp(s) == "pass");
        REQUIRE(getProfileVerifiedOverHttp(s));

        REQUIRE(created["photos"].size() == 6u);
        const auto photoToRemove = created["photos"][0]["id"].asString();

        Json::Value patchBody;
        patchBody["add"] = jsonStringArray({"new-photo"});
        patchBody["remove"] = jsonStringArray({photoToRemove});
        auto patchResp =
            sendTestRequest(s.baseUrl, Patch, "/v1/profile/photos", s.token, &patchBody);
        REQUIRE(patchResp.status == k200OK);
        CHECK(!patchResp.json["verified"].asBool());

        CHECK(!getProfileVerifiedOverHttp(s));
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

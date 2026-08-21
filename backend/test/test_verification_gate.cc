#include <drogon/drogon_test.h>

#include "TestDb.h"
#include "TestHttpFixtures.h"
#include "TestHttpServer.h"

// POST /verification's own gate (profile must exist and have >= 6 photos)
// had zero test coverage before this. All three tests here go through the
// real HTTP endpoint.

using namespace test_support;
using namespace drogon;

// CHECKS: POST /verification returns 400 when the caller has no profile at all
DROGON_TEST(PostVerificationWithoutProfileReturns400)
{
    try
    {
        auto db = testDbClient();
        TestSession s;
        s.userId = createTestUser(db);
        s.baseUrl = ensureTestServerRunning();
        s.token = signTestJwt(s.userId);
        // Deliberately never call PUT /profile for this user.

        auto resp = sendTestRequest(s.baseUrl, Post, "/v1/verification", s.token);
        CHECK(resp.status == k400BadRequest);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: POST /verification returns 400 when the caller's profile has fewer than 6 photos
DROGON_TEST(PostVerificationWithFewerThanSixPhotosReturns400)
{
    try
    {
        auto s = setUpTestSession();
        auto created = putProfileOverHttp(s, sixPhotoUrls("g"));

        // There is no legitimate way to reach a <6-photo profile through
        // this module's own API — PUT /profile and PATCH /profile/photos
        // both refuse to save below the minimum, by design (CUJ #1's
        // "live minimum, not a one-time gate"). That's exactly why
        // VerificationService::startVerification's own count check is
        // otherwise dead code today; this direct DELETE exists purely to
        // construct that precondition so the check itself is exercised.
        // What's actually under test — POST /verification's response —
        // still goes through real HTTP.
        auto db = testDbClient();
        REQUIRE(created["photos"].size() == 6u);
        const auto photoIdToRemove = created["photos"][0]["id"].asString();
        db->execSqlSync("DELETE FROM profile_photos WHERE id = $1", photoIdToRemove);

        auto resp = sendTestRequest(s.baseUrl, Post, "/v1/verification", s.token);
        CHECK(resp.status == k400BadRequest);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: a successful POST /verification actually inserts a row into the verifications table, not just a plausible-looking HTTP response
DROGON_TEST(PostVerificationSuccessInsertsVerificationRow)
{
    try
    {
        auto s = setUpTestSession();
        putProfileOverHttp(s, sixPhotoUrls("i"));

        auto resp = sendTestRequest(s.baseUrl, Post, "/v1/verification", s.token);
        REQUIRE(resp.status == k201Created);
        const auto verificationId = resp.json["id"].asString();
        REQUIRE(!verificationId.empty());

        auto db = testDbClient();
        auto row = db->execSqlSync(
            "SELECT user_id, decision FROM verifications WHERE id = $1", verificationId);
        REQUIRE(row.size() == 1u);
        CHECK(row[0]["user_id"].as<std::string>() == s.userId);
        CHECK(row[0]["decision"].as<std::string>() == "pending");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

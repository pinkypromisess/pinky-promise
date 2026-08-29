#include <drogon/drogon_test.h>

#include <stdexcept>

#include "TestDb.h"
#include "TestHttpFixtures.h"
#include "TestHttpServer.h"

using namespace test_support;
using namespace drogon;

namespace
{
Json::Value buildProfileBody(const std::vector<std::string> &photoUrls,
                              const std::string &occupation = "",
                              const std::string &relationshipStatus = "")
{
    Json::Value body;
    body["name"] = "Test User";
    body["sex"] = "female";
    body["age"] = 30;
    body["need_to_know_text"] = "I love testing.";
    body["photo_urls"] = jsonStringArray(photoUrls);
    if (!occupation.empty())
    {
        body["occupation"] = occupation;
    }
    if (!relationshipStatus.empty())
    {
        body["relationship_status"] = relationshipStatus;
    }
    return body;
}

// Creates a fresh session with a 6-photo profile, verified (StubFaceVerificationProvider
// always passes -- see test_verification_stub.cc), so the caller is eligible to post
// Proposals. Throws (see the throw-not-FAIL convention explained in TestHttpFixtures.h) on
// any unexpected response.
TestSession setUpVerifiedSession(const std::string &occupation = "",
                                  const std::string &relationshipStatus = "")
{
    auto s = setUpTestSession();
    auto body = buildProfileBody(sixPhotoUrls("prop-" + s.userId), occupation, relationshipStatus);
    auto putResp = sendTestRequest(s.baseUrl, Put, "/v1/profile", s.token, &body);
    if (putResp.status != k200OK)
    {
        throw std::runtime_error("PUT /v1/profile returned unexpected status " +
                                  std::to_string(putResp.status));
    }
    auto decision = verifyOverHttp(s);
    if (decision != "pass")
    {
        throw std::runtime_error("verifyOverHttp did not return pass, got " + decision);
    }
    return s;
}

Json::Value validProposalBody(const std::vector<std::string> &revealedFields = {})
{
    Json::Value body;
    body["activity_text"] = "grab coffee";
    body["event_time"] = "2026-09-01T18:00:00Z";
    Json::Value location;
    location["lat"] = 37.7749;
    location["lng"] = -122.4194;
    location["address"] = "123 Main St, San Francisco, CA";
    body["location"] = location;
    body["payment_type"] = "split";
    body["looking_for_text"] = "someone chill to talk with";
    if (!revealedFields.empty())
    {
        body["revealed_fields"] = jsonStringArray(revealedFields);
    }
    return body;
}

// Finds the feed item for `proposalId` in a GET /proposals/feed response, or a null
// Json::Value if absent.
Json::Value findFeedItem(const Json::Value &feedResponse, const std::string &proposalId)
{
    for (const auto &item : feedResponse["proposals"])
    {
        if (item["proposal"]["id"].asString() == proposalId)
        {
            return item;
        }
    }
    return Json::Value(Json::nullValue);
}

}  // namespace

// CHECKS: POST /v1/proposals over real HTTP (real router + auth::AuthFilter + real
// Postgres) is rejected with 403 for a caller whose profile isn't verified yet
DROGON_TEST(CreateProposalWithoutVerificationReturns403)
{
    try
    {
        auto s = setUpTestSession();
        Json::Value profileBody = buildProfileBody(sixPhotoUrls("unverified-" + s.userId));
        auto putResp = sendTestRequest(s.baseUrl, Put, "/v1/profile", s.token, &profileBody);
        REQUIRE(putResp.status == k200OK);

        auto body = validProposalBody();
        auto resp = sendTestRequest(s.baseUrl, Post, "/v1/proposals", s.token, &body);
        REQUIRE(resp.status == k403Forbidden);
        CHECK(resp.json["error"].asString() == "PROFILE_NOT_VERIFIED");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: POST /v1/proposals is rejected with 403 PHOTO_MINIMUM_NOT_MET when the
// caller's live photo count has dropped below 6, even though `verified` is still true
// (simulates the invariant Module A's own endpoints are supposed to prevent -- this is
// the defense-in-depth check the task asked this module to enforce independently)
DROGON_TEST(CreateProposalWithFewerThanSixPhotosReturns403)
{
    try
    {
        auto s = setUpVerifiedSession();

        auto db = testDbClient();
        db->execSqlSync(
            "DELETE FROM profile_photos WHERE user_id = $1 AND position >= 2", s.userId);
        auto remaining =
            db->execSqlSync("SELECT count(*) AS c FROM profile_photos WHERE user_id = $1",
                             s.userId);
        REQUIRE(remaining[0]["c"].as<int64_t>() < 6);

        auto body = validProposalBody();
        auto resp = sendTestRequest(s.baseUrl, Post, "/v1/proposals", s.token, &body);
        REQUIRE(resp.status == k403Forbidden);
        CHECK(resp.json["error"].asString() == "PHOTO_MINIMUM_NOT_MET");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: POST /v1/proposals succeeds (201) for a verified caller with >= 6 photos, and
// echoes back the fields that were submitted
DROGON_TEST(CreateProposalSucceedsForVerifiedCallerWithSixPhotos)
{
    try
    {
        auto s = setUpVerifiedSession();

        auto body = validProposalBody();
        auto resp = sendTestRequest(s.baseUrl, Post, "/v1/proposals", s.token, &body);
        REQUIRE(resp.status == k201Created);
        CHECK(resp.json["creator_user_id"].asString() == s.userId);
        CHECK(resp.json["activity_text"].asString() == "grab coffee");
        CHECK(resp.json["payment_type"].asString() == "split");
        CHECK(resp.json["status"].asString() == "active");
        CHECK(resp.json["revealed_fields"].isArray());
        CHECK(resp.json["revealed_fields"].empty());
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: revealed_fields naming a field the caller's profile hasn't filled in (no
// occupation set) is rejected with 400 REVEALED_FIELD_NOT_FILLED, not silently dropped
DROGON_TEST(CreateProposalRejectsRevealingUnfilledField)
{
    try
    {
        auto s = setUpVerifiedSession();  // no occupation set

        auto body = validProposalBody({"occupation"});
        auto resp = sendTestRequest(s.baseUrl, Post, "/v1/proposals", s.token, &body);
        REQUIRE(resp.status == k400BadRequest);
        CHECK(resp.json["error"].asString() == "REVEALED_FIELD_NOT_FILLED");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: the trickiest rule in this module, end to end over real HTTP + real DB --
// a proposal's feed card shows occupation/relationship_status only when the creator both
// filled them in AND chose to reveal them on that specific proposal; a second, non-owner
// caller is the one reading the feed, matching how this data actually gets consumed
DROGON_TEST(FeedAppliesRevealedFieldsVisibilityRule)
{
    try
    {
        auto creator = setUpVerifiedSession("Software Engineer", "single");

        auto revealedBody = validProposalBody({"occupation"});
        auto revealedResp =
            sendTestRequest(creator.baseUrl, Post, "/v1/proposals", creator.token, &revealedBody);
        REQUIRE(revealedResp.status == k201Created);
        const auto revealedProposalId = revealedResp.json["id"].asString();

        auto hiddenBody = validProposalBody();  // revealed_fields omitted -> stays hidden
        auto hiddenResp =
            sendTestRequest(creator.baseUrl, Post, "/v1/proposals", creator.token, &hiddenBody);
        REQUIRE(hiddenResp.status == k201Created);
        const auto hiddenProposalId = hiddenResp.json["id"].asString();

        auto viewer = setUpTestSession();
        auto feedResp = sendTestRequest(viewer.baseUrl, Get, "/v1/proposals/feed", viewer.token);
        REQUIRE(feedResp.status == k200OK);

        auto revealedItem = findFeedItem(feedResp.json, revealedProposalId);
        REQUIRE(!revealedItem.isNull());
        CHECK(revealedItem["creator"]["occupation"].asString() == "Software Engineer");
        CHECK(revealedItem["creator"]["relationship_status"].isNull());  // not in revealed_fields
        CHECK(revealedItem["creator"]["name"].asString() == "Test User");
        CHECK(revealedItem["creator"]["photos"].size() == 6u);

        auto hiddenItem = findFeedItem(feedResp.json, hiddenProposalId);
        REQUIRE(!hiddenItem.isNull());
        CHECK(hiddenItem["creator"]["occupation"].isNull());
        CHECK(hiddenItem["creator"]["relationship_status"].isNull());
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: GET /v1/proposals/feed excludes the caller's own proposals
DROGON_TEST(FeedExcludesCallersOwnProposals)
{
    try
    {
        auto s = setUpVerifiedSession();
        auto body = validProposalBody();
        auto createResp = sendTestRequest(s.baseUrl, Post, "/v1/proposals", s.token, &body);
        REQUIRE(createResp.status == k201Created);
        const auto proposalId = createResp.json["id"].asString();

        auto feedResp = sendTestRequest(s.baseUrl, Get, "/v1/proposals/feed", s.token);
        REQUIRE(feedResp.status == k200OK);
        CHECK(findFeedItem(feedResp.json, proposalId).isNull());
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: DELETE /v1/proposals/{id} by the creator sets status to cancelled and removes
// it from other users' feeds
DROGON_TEST(DeleteProposalByCreatorCancelsAndHidesFromFeed)
{
    try
    {
        auto creator = setUpVerifiedSession();
        auto body = validProposalBody();
        auto createResp =
            sendTestRequest(creator.baseUrl, Post, "/v1/proposals", creator.token, &body);
        REQUIRE(createResp.status == k201Created);
        const auto proposalId = createResp.json["id"].asString();

        auto deleteResp = sendTestRequest(
            creator.baseUrl, Delete, "/v1/proposals/" + proposalId, creator.token);
        CHECK(deleteResp.status == k204NoContent);

        auto db = testDbClient();
        auto row =
            db->execSqlSync("SELECT status FROM proposals WHERE id = $1", proposalId);
        REQUIRE(!row.empty());
        CHECK(row[0]["status"].as<std::string>() == "cancelled");

        auto viewer = setUpTestSession();
        auto feedResp = sendTestRequest(viewer.baseUrl, Get, "/v1/proposals/feed", viewer.token);
        REQUIRE(feedResp.status == k200OK);
        CHECK(findFeedItem(feedResp.json, proposalId).isNull());
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: DELETE /v1/proposals/{id} by a user who isn't the creator returns 404, and
// leaves the proposal untouched
DROGON_TEST(DeleteProposalByNonCreatorReturns404)
{
    try
    {
        auto creator = setUpVerifiedSession();
        auto body = validProposalBody();
        auto createResp =
            sendTestRequest(creator.baseUrl, Post, "/v1/proposals", creator.token, &body);
        REQUIRE(createResp.status == k201Created);
        const auto proposalId = createResp.json["id"].asString();

        auto intruder = setUpTestSession();
        auto deleteResp = sendTestRequest(
            intruder.baseUrl, Delete, "/v1/proposals/" + proposalId, intruder.token);
        CHECK(deleteResp.status == k404NotFound);

        auto db = testDbClient();
        auto row = db->execSqlSync("SELECT status FROM proposals WHERE id = $1", proposalId);
        REQUIRE(!row.empty());
        CHECK(row[0]["status"].as<std::string>() == "active");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: all three Proposal routes are behind auth::AuthFilter -- no bearer token means
// 401 before ever reaching the controller
DROGON_TEST(ProposalRoutesWithoutAuthHeaderReturn401)
{
    try
    {
        auto baseUrl = ensureTestServerRunning();

        auto feedResp = sendTestRequest(baseUrl, Get, "/v1/proposals/feed", /*bearerToken=*/"");
        CHECK(feedResp.status == k401Unauthorized);

        auto body = validProposalBody();
        auto createResp = sendTestRequest(baseUrl, Post, "/v1/proposals", /*bearerToken=*/"", &body);
        CHECK(createResp.status == k401Unauthorized);

        auto deleteResp =
            sendTestRequest(baseUrl, Delete, "/v1/proposals/nonexistent-id", /*bearerToken=*/"");
        CHECK(deleteResp.status == k401Unauthorized);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

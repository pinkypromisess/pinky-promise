#include <drogon/drogon_test.h>

#include <stdexcept>
#include <string>

#include "TestDb.h"
#include "TestHttpFixtures.h"
#include "TestHttpServer.h"

using namespace test_support;
using namespace drogon;

// Every DROGON_TEST in this file is HTTP-LEVEL: the request goes through
// the real Drogon router + the real auth::AuthFilter + a real Postgres DB
// (via the TestHttpServer harness), exactly how a mobile client would hit
// POST /v1/proposals/{id}/swipe. Pure-logic coverage of the action/uuid
// validators lives in test_swipe_validation.cc.

namespace
{
Json::Value buildProfileBody(const std::vector<std::string> &photoUrls)
{
    Json::Value body;
    body["name"] = "Test User";
    body["sex"] = "female";
    body["age"] = 30;
    body["need_to_know_text"] = "I love testing.";
    body["photo_urls"] = jsonStringArray(photoUrls);
    return body;
}

// A session with a 6-photo profile but NOT verified.
TestSession setUpProfiledSession()
{
    auto s = setUpTestSession();
    auto body = buildProfileBody(sixPhotoUrls("swipe-" + s.userId));
    auto putResp = sendTestRequest(s.baseUrl, Put, "/v1/profile", s.token, &body);
    if (putResp.status != k200OK)
    {
        throw std::runtime_error("PUT /v1/profile returned unexpected status " +
                                  std::to_string(putResp.status));
    }
    return s;
}

// A session with a 6-photo profile that has passed verification
// (StubFaceVerificationProvider always passes).
TestSession setUpVerifiedSession()
{
    auto s = setUpProfiledSession();
    auto decision = verifyOverHttp(s);
    if (decision != "pass")
    {
        throw std::runtime_error("verifyOverHttp did not return pass, got " + decision);
    }
    return s;
}

Json::Value validProposalBody()
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
    return body;
}

// Creates an active proposal owned by `creator` over HTTP and returns its id.
std::string createProposalOverHttp(const TestSession &creator)
{
    auto body = validProposalBody();
    auto resp = sendTestRequest(creator.baseUrl, Post, "/v1/proposals", creator.token, &body);
    if (resp.status != k201Created)
    {
        throw std::runtime_error("POST /v1/proposals returned unexpected status " +
                                  std::to_string(resp.status));
    }
    return resp.json["id"].asString();
}

Json::Value swipeBody(const std::string &action)
{
    Json::Value body;
    body["action"] = action;
    return body;
}

HttpTestResponse swipeOverHttp(const TestSession &s,
                                const std::string &proposalId,
                                const std::string &action)
{
    auto body = swipeBody(action);
    return sendTestRequest(
        s.baseUrl, Post, "/v1/proposals/" + proposalId + "/swipe", s.token, &body);
}

// Directly seeds `count` 'interested' swipe rows for `swiperUserId` on
// `count` freshly-inserted proposals owned by `creatorUserId`, all in one
// parameterized statement -- used to drive the caller up to the rolling-24h
// cap boundary without making dozens of HTTP calls.
void seedInterestedSwipes(const std::string &creatorUserId,
                           const std::string &swiperUserId,
                           int count)
{
    auto db = testDbClient();
    db->execSqlSync(
        "WITH seeded AS ("
        "  INSERT INTO proposals (creator_user_id, activity_text, event_time, "
        "    location_lat, location_lng, location_address, payment_type, looking_for_text) "
        "  SELECT $1, 'seed', now() + interval '1 day', 0, 0, 'seed addr', 'split', 'seed' "
        "  FROM generate_series(1, $2) "
        "  RETURNING id"
        ") "
        "INSERT INTO swipes (proposal_id, swiper_user_id, action) "
        "SELECT id, $3, 'interested' FROM seeded",
        creatorUserId,
        count,
        swiperUserId);
}

}  // namespace

// CHECKS (HTTP-level): 'pass' is recorded (201) even when the caller's
// profile is unverified -- browsing/passing is never gated (CUJ #2).
DROGON_TEST(PassSwipeAllowedWhileUnverified)
{
    try
    {
        auto creator = setUpVerifiedSession();
        auto proposalId = createProposalOverHttp(creator);

        auto swiper = setUpProfiledSession();  // NOT verified
        auto resp = swipeOverHttp(swiper, proposalId, "pass");
        REQUIRE(resp.status == k201Created);
        CHECK(resp.json["proposal_id"].asString() == proposalId);
        CHECK(resp.json["action"].asString() == "pass");
        CHECK(!resp.json["id"].asString().empty());
        CHECK(!resp.json["created_at"].asString().empty());
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// REQUIRED CHECKPOINT TEST (a) -- verified-gate 403.
// CHECKS (HTTP-level): 'interested' by an unverified caller is rejected
// 403 PROFILE_NOT_VERIFIED, and no swipe row is written.
DROGON_TEST(InterestedSwipeRejectedWhenUnverified)
{
    try
    {
        auto creator = setUpVerifiedSession();
        auto proposalId = createProposalOverHttp(creator);

        auto swiper = setUpProfiledSession();  // NOT verified
        auto resp = swipeOverHttp(swiper, proposalId, "interested");
        REQUIRE(resp.status == k403Forbidden);
        CHECK(resp.json["error"].asString() == "PROFILE_NOT_VERIFIED");

        auto db = testDbClient();
        auto rows = db->execSqlSync(
            "SELECT count(*) AS c FROM swipes WHERE proposal_id = $1 AND swiper_user_id = $2",
            proposalId,
            swiper.userId);
        CHECK(rows[0]["c"].as<int64_t>() == 0);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS (HTTP-level): 'interested' by a verified caller with room under
// the cap is recorded (201) with the created swipe echoed back.
DROGON_TEST(InterestedSwipeSucceedsForVerifiedCaller)
{
    try
    {
        auto creator = setUpVerifiedSession();
        auto proposalId = createProposalOverHttp(creator);

        auto swiper = setUpVerifiedSession();
        auto resp = swipeOverHttp(swiper, proposalId, "interested");
        REQUIRE(resp.status == k201Created);
        CHECK(resp.json["proposal_id"].asString() == proposalId);
        CHECK(resp.json["action"].asString() == "interested");

        auto db = testDbClient();
        auto rows = db->execSqlSync(
            "SELECT action FROM swipes WHERE proposal_id = $1 AND swiper_user_id = $2",
            proposalId,
            swiper.userId);
        REQUIRE(rows.size() == 1);
        CHECK(rows[0]["action"].as<std::string>() == "interested");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// REQUIRED CHECKPOINT TEST (b) -- 10/day 'interested' cap.
// CHECKS (HTTP-level): with 9 'interested' swipes already in the last 24h,
// the 10th over HTTP succeeds (201) and the 11th is rejected 403
// INTERESTED_DAILY_CAP_REACHED (a distinct code from the verified gate).
// 'pass' is unaffected by the cap.
DROGON_TEST(InterestedSwipeRollingDailyCap)
{
    try
    {
        auto creator = setUpVerifiedSession();
        auto swiper = setUpVerifiedSession();

        seedInterestedSwipes(creator.userId, swiper.userId, 9);

        auto tenthProposal = createProposalOverHttp(creator);
        auto tenth = swipeOverHttp(swiper, tenthProposal, "interested");
        REQUIRE(tenth.status == k201Created);

        auto eleventhProposal = createProposalOverHttp(creator);
        auto eleventh = swipeOverHttp(swiper, eleventhProposal, "interested");
        REQUIRE(eleventh.status == k403Forbidden);
        CHECK(eleventh.json["error"].asString() == "INTERESTED_DAILY_CAP_REACHED");

        // 'pass' still goes through even when the interested cap is hit.
        auto passResp = swipeOverHttp(swiper, eleventhProposal, "pass");
        REQUIRE(passResp.status == k201Created);
        CHECK(passResp.json["action"].asString() == "pass");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS (HTTP-level): 'interested' swipes older than the rolling 24h
// window do NOT count toward the cap -- 15 stale rows still leave the
// caller able to swipe 'interested'.
DROGON_TEST(StaleInterestedSwipesDoNotCountTowardCap)
{
    try
    {
        auto creator = setUpVerifiedSession();
        auto swiper = setUpVerifiedSession();

        seedInterestedSwipes(creator.userId, swiper.userId, 15);
        auto db = testDbClient();
        db->execSqlSync(
            "UPDATE swipes SET created_at = now() - interval '25 hours' "
            "WHERE swiper_user_id = $1",
            swiper.userId);

        auto proposalId = createProposalOverHttp(creator);
        auto resp = swipeOverHttp(swiper, proposalId, "interested");
        REQUIRE(resp.status == k201Created);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// REQUIRED CHECKPOINT TEST (c) -- UNIQUE (proposal_id, swiper_user_id).
// CHECKS (HTTP-level): a second swipe by the same user on the same
// proposal is rejected 409 ALREADY_SWIPED (not upserted / no-op'd); the
// first swipe's row and action are left untouched.
DROGON_TEST(SecondSwipeOnSameProposalRejected409)
{
    try
    {
        auto creator = setUpVerifiedSession();
        auto proposalId = createProposalOverHttp(creator);

        auto swiper = setUpVerifiedSession();
        auto first = swipeOverHttp(swiper, proposalId, "pass");
        REQUIRE(first.status == k201Created);

        // Same proposal, different action -- still a duplicate.
        auto second = swipeOverHttp(swiper, proposalId, "interested");
        REQUIRE(second.status == k409Conflict);
        CHECK(second.json["error"].asString() == "ALREADY_SWIPED");

        auto db = testDbClient();
        auto rows = db->execSqlSync(
            "SELECT action FROM swipes WHERE proposal_id = $1 AND swiper_user_id = $2",
            proposalId,
            swiper.userId);
        REQUIRE(rows.size() == 1);
        CHECK(rows[0]["action"].as<std::string>() == "pass");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS (HTTP-level): swiping on an unknown proposal id (well-formed uuid
// that matches nothing, and a plainly malformed id) both return 404
// PROPOSAL_NOT_FOUND.
DROGON_TEST(SwipeOnUnknownProposalReturns404)
{
    try
    {
        auto swiper = setUpVerifiedSession();

        auto unknown =
            swipeOverHttp(swiper, "123e4567-e89b-12d3-a456-426614174000", "pass");
        CHECK(unknown.status == k404NotFound);
        CHECK(unknown.json["error"].asString() == "PROPOSAL_NOT_FOUND");

        auto malformed = swipeOverHttp(swiper, "not-a-real-id", "pass");
        CHECK(malformed.status == k404NotFound);
        CHECK(malformed.json["error"].asString() == "PROPOSAL_NOT_FOUND");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS (HTTP-level): swiping on a proposal that exists but isn't active
// (creator deleted it -> status 'cancelled') returns 404.
DROGON_TEST(SwipeOnInactiveProposalReturns404)
{
    try
    {
        auto creator = setUpVerifiedSession();
        auto proposalId = createProposalOverHttp(creator);
        auto del = sendTestRequest(
            creator.baseUrl, Delete, "/v1/proposals/" + proposalId, creator.token);
        REQUIRE(del.status == k204NoContent);

        auto swiper = setUpVerifiedSession();
        auto resp = swipeOverHttp(swiper, proposalId, "pass");
        CHECK(resp.status == k404NotFound);
        CHECK(resp.json["error"].asString() == "PROPOSAL_NOT_FOUND");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS (HTTP-level): swiping on your own proposal is rejected 400
// CANNOT_SWIPE_OWN_PROPOSAL, and writes no row.
DROGON_TEST(SwipeOnOwnProposalRejected400)
{
    try
    {
        auto creator = setUpVerifiedSession();
        auto proposalId = createProposalOverHttp(creator);

        auto resp = swipeOverHttp(creator, proposalId, "interested");
        REQUIRE(resp.status == k400BadRequest);
        CHECK(resp.json["error"].asString() == "CANNOT_SWIPE_OWN_PROPOSAL");

        auto passResp = swipeOverHttp(creator, proposalId, "pass");
        CHECK(passResp.status == k400BadRequest);
        CHECK(passResp.json["error"].asString() == "CANNOT_SWIPE_OWN_PROPOSAL");

        auto db = testDbClient();
        auto rows = db->execSqlSync(
            "SELECT count(*) AS c FROM swipes WHERE proposal_id = $1", proposalId);
        CHECK(rows[0]["c"].as<int64_t>() == 0);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS (HTTP-level): a body with an action outside {interested, pass},
// and a request with no JSON body, are both rejected 400 before any DB
// work.
DROGON_TEST(SwipeWithBadActionRejected400)
{
    try
    {
        auto creator = setUpVerifiedSession();
        auto proposalId = createProposalOverHttp(creator);
        auto swiper = setUpVerifiedSession();

        auto bad = swipeOverHttp(swiper, proposalId, "maybe");
        REQUIRE(bad.status == k400BadRequest);
        CHECK(bad.json["error"].asString() == "ACTION_INVALID");

        auto noBody = sendTestRequest(
            swiper.baseUrl, Post, "/v1/proposals/" + proposalId + "/swipe", swiper.token);
        CHECK(noBody.status == k400BadRequest);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS (HTTP-level): the swipe route is behind auth::AuthFilter -- no
// bearer token means 401 before the controller runs.
DROGON_TEST(SwipeWithoutAuthHeaderReturns401)
{
    try
    {
        auto baseUrl = ensureTestServerRunning();
        auto body = swipeBody("pass");
        auto resp = sendTestRequest(
            baseUrl,
            Post,
            "/v1/proposals/123e4567-e89b-12d3-a456-426614174000/swipe",
            /*bearerToken=*/"",
            &body);
        CHECK(resp.status == k401Unauthorized);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

#include <drogon/drogon_test.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "TestDb.h"
#include "TestHttpFixtures.h"
#include "TestHttpServer.h"

// Every DROGON_TEST here is HTTP-LEVEL: the request goes through the real
// Drogon router + the real auth::AuthFilter + a real Postgres DB (via the
// TestHttpServer harness). These cover Module E.3's two Module B/C
// unblock edits: ProposalService::getFeed's blocked-creator exclusion,
// and SwipeService::recordSwipe's BLOCKED gate on a new `interested`
// swipe. Kept in a new file (not test_http_proposals.cc /
// test_http_swipe.cc, Module B/C's own test files) per the E.3 brief,
// same as C.4 added a new file instead of editing theirs.

using namespace test_support;
using namespace drogon;

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

TestSession setUpVerifiedSession()
{
    auto s = setUpTestSession();
    auto body = buildProfileBody(sixPhotoUrls("blkfs-" + s.userId));
    auto put = sendTestRequest(s.baseUrl, Put, "/v1/profile", s.token, &body);
    if (put.status != k200OK)
    {
        throw std::runtime_error("PUT /v1/profile -> " + std::to_string(put.status));
    }
    if (verifyOverHttp(s) != "pass")
    {
        throw std::runtime_error("verifyOverHttp did not pass");
    }
    return s;
}

Json::Value validProposalBody()
{
    Json::Value body;
    body["activity_text"] = "grab coffee";
    body["event_time"] = "2027-01-01T18:00:00Z";
    Json::Value loc;
    loc["lat"] = 37.7749;
    loc["lng"] = -122.4194;
    loc["address"] = "123 Main St, San Francisco, CA";
    body["location"] = loc;
    body["payment_type"] = "split";
    body["looking_for_text"] = "someone chill to talk with";
    return body;
}

std::string createProposalOverHttp(const TestSession &creator)
{
    auto body = validProposalBody();
    auto resp = sendTestRequest(creator.baseUrl, Post, "/v1/proposals", creator.token, &body);
    if (resp.status != k201Created)
    {
        throw std::runtime_error("POST /v1/proposals -> " + std::to_string(resp.status));
    }
    return resp.json["id"].asString();
}

HttpTestResponse swipe(const TestSession &s, const std::string &proposalId,
                       const std::string &action)
{
    Json::Value body;
    body["action"] = action;
    return sendTestRequest(
        s.baseUrl, Post, "/v1/proposals/" + proposalId + "/swipe", s.token, &body);
}

HttpTestResponse blockUser(const TestSession &s, const std::string &blockedUserId)
{
    Json::Value body;
    body["blocked_user_id"] = blockedUserId;
    return sendTestRequest(s.baseUrl, Post, "/v1/blocks", s.token, &body);
}

std::vector<std::string> feedProposalIds(const TestSession &s)
{
    auto resp = sendTestRequest(s.baseUrl, Get, "/v1/proposals/feed", s.token);
    if (resp.status != k200OK)
    {
        throw std::runtime_error("GET /v1/proposals/feed -> " + std::to_string(resp.status));
    }
    std::vector<std::string> ids;
    for (const auto &item : resp.json["proposals"])
    {
        ids.push_back(item["proposal"]["id"].asString());
    }
    return ids;
}

bool contains(const std::vector<std::string> &v, const std::string &x)
{
    return std::find(v.begin(), v.end(), x) != v.end();
}

bool feedContains(const TestSession &s, const std::string &proposalId)
{
    return contains(feedProposalIds(s), proposalId);
}

int64_t dbSwipeRowCount(const std::string &proposalId, const std::string &swiperUserId)
{
    auto rows = testDbClient()->execSqlSync(
        "SELECT count(*) AS c FROM swipes WHERE proposal_id = $1 AND swiper_user_id = $2",
        proposalId,
        swiperUserId);
    return rows[0]["c"].as<int64_t>();
}

int64_t dbConversationRowCount(const std::string &proposalId)
{
    auto rows = testDbClient()->execSqlSync(
        "SELECT count(*) AS c FROM conversations WHERE proposal_id = $1", proposalId);
    return rows[0]["c"].as<int64_t>();
}

}  // namespace

// REQUIRED 1: feed excludes a proposal from a creator the caller has
// blocked.
DROGON_TEST(FeedExcludesProposalFromCreatorCallerBlocked)
{
    try
    {
        auto caller = setUpVerifiedSession();
        auto creator = setUpVerifiedSession();
        auto pid = createProposalOverHttp(creator);

        REQUIRE(feedContains(caller, pid));
        REQUIRE(blockUser(caller, creator.userId).status == k201Created);
        CHECK(!feedContains(caller, pid));
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 2: feed excludes a proposal from a creator who has blocked the
// caller -- the OTHER direction.
DROGON_TEST(FeedExcludesProposalFromCreatorWhoBlockedCaller)
{
    try
    {
        auto caller = setUpVerifiedSession();
        auto creator = setUpVerifiedSession();
        auto pid = createProposalOverHttp(creator);

        REQUIRE(feedContains(caller, pid));
        // creator blocks caller, not the other way around.
        REQUIRE(blockUser(creator, caller.userId).status == k201Created);
        CHECK(!feedContains(caller, pid));
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 3: feed still shows a proposal from an unrelated, unblocked
// creator (no over-filtering), and the existing C.4 swipe-exclusion still
// composes correctly alongside the new block exclusion.
DROGON_TEST(FeedKeepsUnblockedProposalAndComposesWithSwipeExclusion)
{
    try
    {
        auto caller = setUpVerifiedSession();
        auto blockedCreator = setUpVerifiedSession();
        auto unrelatedCreator = setUpVerifiedSession();

        auto blockedPid = createProposalOverHttp(blockedCreator);
        auto swipedPid = createProposalOverHttp(unrelatedCreator);
        auto visiblePid = createProposalOverHttp(unrelatedCreator);

        REQUIRE(blockUser(caller, blockedCreator.userId).status == k201Created);
        REQUIRE(swipe(caller, swipedPid, "pass").status == k201Created);

        auto ids = feedProposalIds(caller);
        CHECK(!contains(ids, blockedPid));  // blocked creator -> excluded (E.3)
        CHECK(!contains(ids, swipedPid));   // already swiped -> excluded (C.4)
        CHECK(contains(ids, visiblePid));   // unrelated, unswiped -> present
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 4: a blocked pair -- caller attempts `interested` on the
// blocked creator's proposal (simulating a direct API call bypassing the
// feed) -> 403 BLOCKED, no swipe row, no conversation created.
DROGON_TEST(InterestedSwipeOnBlockedCreatorsProposalRejected)
{
    try
    {
        auto caller = setUpVerifiedSession();
        auto creator = setUpVerifiedSession();
        auto pid = createProposalOverHttp(creator);

        REQUIRE(blockUser(caller, creator.userId).status == k201Created);

        auto resp = swipe(caller, pid, "interested");
        CHECK(resp.status == k403Forbidden);
        CHECK(resp.json["error"].asString() == "BLOCKED");

        CHECK(dbSwipeRowCount(pid, caller.userId) == 0);
        CHECK(dbConversationRowCount(pid) == 0);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 5: same, other direction -- the blocker (creator) attempting
// `interested` on a proposal posted by the user they blocked.
DROGON_TEST(InterestedSwipeByBlockerOnBlockedUsersProposalRejected)
{
    try
    {
        auto blocker = setUpVerifiedSession();
        auto blocked = setUpVerifiedSession();
        // `blocked` posts the proposal; `blocker` (who blocked them)
        // tries to swipe interested on it.
        auto pid = createProposalOverHttp(blocked);

        REQUIRE(blockUser(blocker, blocked.userId).status == k201Created);

        auto resp = swipe(blocker, pid, "interested");
        CHECK(resp.status == k403Forbidden);
        CHECK(resp.json["error"].asString() == "BLOCKED");

        CHECK(dbSwipeRowCount(pid, blocker.userId) == 0);
        CHECK(dbConversationRowCount(pid) == 0);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 6: `pass` between a blocked pair still succeeds -- not gated.
DROGON_TEST(PassSwipeBetweenBlockedPairStillSucceeds)
{
    try
    {
        auto caller = setUpVerifiedSession();
        auto creator = setUpVerifiedSession();
        auto pid = createProposalOverHttp(creator);

        REQUIRE(blockUser(caller, creator.userId).status == k201Created);

        auto resp = swipe(caller, pid, "pass");
        CHECK(resp.status == k201Created);
        CHECK(dbSwipeRowCount(pid, caller.userId) == 1);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 7: an unrelated, unblocked pair's `interested` swipe still
// succeeds normally -- no false-positive blocking.
DROGON_TEST(InterestedSwipeBetweenUnblockedPairStillSucceeds)
{
    try
    {
        auto caller = setUpVerifiedSession();
        auto creator = setUpVerifiedSession();
        auto pid = createProposalOverHttp(creator);

        auto resp = swipe(caller, pid, "interested");
        CHECK(resp.status == k201Created);
        CHECK(dbSwipeRowCount(pid, caller.userId) == 1);
        CHECK(dbConversationRowCount(pid) == 1);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

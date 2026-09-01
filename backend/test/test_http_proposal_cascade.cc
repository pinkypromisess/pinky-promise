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
// TestHttpServer harness). These cover the two "Module C unblock" (C.4)
// changes in ProposalService: the feed's already-swiped anti-join and the
// DELETE /proposals/{id} cascade. A couple of preconditions that the real
// API can't produce (an active conversation co-existing with a confirmed
// PinkyPromise on the same proposal -- confirm() sibling-closes the
// others) are seeded directly through the DB harness.

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
    auto body = buildProfileBody(sixPhotoUrls("cascade-" + s.userId));
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

HttpTestResponse deleteProposal(const TestSession &s, const std::string &proposalId)
{
    return sendTestRequest(s.baseUrl, Delete, "/v1/proposals/" + proposalId, s.token);
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

std::string conversationIdFor(const TestSession &s, const std::string &proposalId)
{
    auto list = sendTestRequest(s.baseUrl, Get, "/v1/conversations", s.token);
    if (list.status != k200OK)
    {
        throw std::runtime_error("GET /v1/conversations -> " + std::to_string(list.status));
    }
    for (const auto &c : list.json["conversations"])
    {
        if (c["proposal_id"].asString() == proposalId)
        {
            return c["id"].asString();
        }
    }
    throw std::runtime_error("no conversation for proposal " + proposalId);
}

HttpTestResponse postMessage(const TestSession &s, const std::string &cid,
                              const std::string &content)
{
    Json::Value b;
    b["type"] = "text";
    b["content"] = content;
    return sendTestRequest(s.baseUrl, Post, "/v1/conversations/" + cid + "/messages", s.token, &b);
}

HttpTestResponse initiatePp(const TestSession &s, const std::string &cid)
{
    return sendTestRequest(
        s.baseUrl, Post, "/v1/conversations/" + cid + "/pinky-promise", s.token);
}

HttpTestResponse confirmPp(const TestSession &s, const std::string &ppId)
{
    return sendTestRequest(s.baseUrl, Post, "/v1/pinky-promises/" + ppId + "/confirm", s.token);
}

std::string dbScalar(const std::string &sql, const std::string &arg)
{
    auto rows = testDbClient()->execSqlSync(sql, arg);
    return rows[0][0].as<std::string>();
}

// A verified A posts a proposal, verified B swipes `interested` -> an
// active conversation. Returns {proposalId, conversationId}.
struct PC
{
    std::string proposalId;
    std::string conversationId;
};

PC setUpActiveConversation(const TestSession &a, const TestSession &b)
{
    PC pc;
    pc.proposalId = createProposalOverHttp(a);
    if (swipe(b, pc.proposalId, "interested").status != k201Created)
    {
        throw std::runtime_error("interested swipe failed");
    }
    pc.conversationId = conversationIdFor(b, pc.proposalId);
    return pc;
}

// Directly inserts a confirmed PinkyPromise on (proposal, conversation,
// A, B). Used where the real confirm() flow can't leave an *active*
// conversation next to a confirmed PP on the same proposal. Parameterized.
std::string dbSeedConfirmedPp(const std::string &proposalId, const std::string &conversationId,
                               const std::string &aId, const std::string &bId)
{
    return testDbClient()
        ->execSqlSync(
            "INSERT INTO pinky_promises (proposal_id, conversation_id, user_a_id, user_b_id, "
            "status, confirmed_at) VALUES ($1, $2, $3, $4, 'confirmed', now()) RETURNING id",
            proposalId,
            conversationId,
            aId,
            bId)[0]["id"]
        .as<std::string>();
}

}  // namespace

// REQUIRED 1: the feed omits a proposal the caller swiped `interested`.
DROGON_TEST(FeedExcludesProposalCallerSwipedInterested)
{
    try
    {
        auto creator = setUpVerifiedSession();
        auto viewer = setUpVerifiedSession();
        auto pid = createProposalOverHttp(creator);

        REQUIRE(feedContains(viewer, pid));  // visible before swiping
        REQUIRE(swipe(viewer, pid, "interested").status == k201Created);
        CHECK(!feedContains(viewer, pid));   // hidden after
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 2: the feed omits a proposal the caller swiped `pass`.
DROGON_TEST(FeedExcludesProposalCallerSwipedPass)
{
    try
    {
        auto creator = setUpVerifiedSession();
        auto viewer = setUpVerifiedSession();
        auto pid = createProposalOverHttp(creator);

        REQUIRE(feedContains(viewer, pid));
        REQUIRE(swipe(viewer, pid, "pass").status == k201Created);
        CHECK(!feedContains(viewer, pid));
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 3: no over-filtering -- an unswiped active proposal is still
// returned, while the caller's own and non-active proposals stay excluded
// (existing Module B behaviour, verified not assumed).
DROGON_TEST(FeedKeepsUnswipedAndPreservesModuleBExclusions)
{
    try
    {
        auto creator = setUpVerifiedSession();
        auto viewer = setUpVerifiedSession();

        auto unswiped = createProposalOverHttp(creator);
        auto swiped = createProposalOverHttp(creator);
        auto ownByViewer = createProposalOverHttp(viewer);
        auto cancelled = createProposalOverHttp(creator);

        REQUIRE(swipe(viewer, swiped, "pass").status == k201Created);
        REQUIRE(deleteProposal(creator, cancelled).status == k204NoContent);

        auto ids = feedProposalIds(viewer);
        CHECK(contains(ids, unswiped));      // not swiped -> present
        CHECK(!contains(ids, swiped));       // swiped -> excluded (C.4)
        CHECK(!contains(ids, ownByViewer));  // own -> excluded (Module B)
        CHECK(!contains(ids, cancelled));    // cancelled -> excluded (Module B)
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 4: the exclusion is per-caller -- a proposal X swiped still
// shows in Y's feed.
DROGON_TEST(FeedExclusionIsPerCaller)
{
    try
    {
        auto creator = setUpVerifiedSession();
        auto x = setUpVerifiedSession();
        auto y = setUpVerifiedSession();
        auto pid = createProposalOverHttp(creator);

        REQUIRE(swipe(x, pid, "interested").status == k201Created);
        CHECK(!feedContains(x, pid));
        CHECK(feedContains(y, pid));
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 5: deleting a proposal with an active conversation flips that
// conversation to `expired`, and (per C.2) posting a message to it now
// 409s.
DROGON_TEST(DeleteCascadesActiveConversationToExpired)
{
    try
    {
        auto a = setUpVerifiedSession();
        auto b = setUpVerifiedSession();
        auto pc = setUpActiveConversation(a, b);
        REQUIRE(dbScalar("SELECT status FROM conversations WHERE id = $1", pc.conversationId) ==
                "active");

        REQUIRE(deleteProposal(a, pc.proposalId).status == k204NoContent);

        CHECK(dbScalar("SELECT status FROM conversations WHERE id = $1", pc.conversationId) ==
              "expired");
        auto post = postMessage(a, pc.conversationId, "still on?");
        CHECK(post.status == k409Conflict);
        CHECK(post.json["error"].asString() == "CONVERSATION_EXPIRED");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 6: deleting a proposal with a confirmed PinkyPromise flips that
// PP to `cancelled`. Uses the real initiate/confirm flow.
DROGON_TEST(DeleteCascadesConfirmedPinkyPromiseToCancelled)
{
    try
    {
        auto a = setUpVerifiedSession();
        auto b = setUpVerifiedSession();
        auto pc = setUpActiveConversation(a, b);

        auto init = initiatePp(a, pc.conversationId);
        REQUIRE(init.status == k201Created);
        const auto ppId = init.json["id"].asString();
        REQUIRE(confirmPp(b, ppId).status == k200OK);
        REQUIRE(dbScalar("SELECT status FROM pinky_promises WHERE id = $1", ppId) == "confirmed");

        REQUIRE(deleteProposal(a, pc.proposalId).status == k204NoContent);

        CHECK(dbScalar("SELECT status FROM pinky_promises WHERE id = $1", ppId) == "cancelled");
        CHECK(dbScalar("SELECT status FROM proposals WHERE id = $1", pc.proposalId) ==
              "cancelled");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 7a: a successful DELETE moves proposal + its active
// conversation + its confirmed PinkyPromise together (seeded precondition:
// the real confirm() flow can't leave an active conversation beside a
// confirmed PP on the same proposal).
DROGON_TEST(DeleteCascadeMovesAllThreeTogether)
{
    try
    {
        auto a = setUpVerifiedSession();
        auto b = setUpVerifiedSession();
        auto pc = setUpActiveConversation(a, b);
        auto ppId = dbSeedConfirmedPp(pc.proposalId, pc.conversationId, a.userId, b.userId);

        REQUIRE(deleteProposal(a, pc.proposalId).status == k204NoContent);

        CHECK(dbScalar("SELECT status FROM proposals WHERE id = $1", pc.proposalId) ==
              "cancelled");
        CHECK(dbScalar("SELECT status FROM conversations WHERE id = $1", pc.conversationId) ==
              "expired");
        CHECK(dbScalar("SELECT status FROM pinky_promises WHERE id = $1", ppId) == "cancelled");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 7b: DELETE by a non-creator 404s and changes nothing -- the
// ownership guard short-circuits the whole atomic statement.
DROGON_TEST(DeleteByNonCreatorReturns404AndCascadesNothing)
{
    try
    {
        auto a = setUpVerifiedSession();
        auto b = setUpVerifiedSession();
        auto intruder = setUpVerifiedSession();
        auto pc = setUpActiveConversation(a, b);
        auto ppId = dbSeedConfirmedPp(pc.proposalId, pc.conversationId, a.userId, b.userId);

        REQUIRE(deleteProposal(intruder, pc.proposalId).status == k404NotFound);

        CHECK(dbScalar("SELECT status FROM proposals WHERE id = $1", pc.proposalId) == "active");
        CHECK(dbScalar("SELECT status FROM conversations WHERE id = $1", pc.conversationId) ==
              "active");
        CHECK(dbScalar("SELECT status FROM pinky_promises WHERE id = $1", ppId) == "confirmed");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 8: a `pending_b_confirm` PinkyPromise on a deleted proposal is
// left untouched (spec cascades only `confirmed` -> `cancelled`); its
// conversation still goes `expired`, which already blocks confirming it
// (the C.3 confirm-time expiry gate then 409s).
DROGON_TEST(DeleteLeavesPendingPinkyPromiseButExpiresItsConversation)
{
    try
    {
        auto a = setUpVerifiedSession();
        auto b = setUpVerifiedSession();
        auto pc = setUpActiveConversation(a, b);

        auto init = initiatePp(a, pc.conversationId);
        REQUIRE(init.status == k201Created);
        const auto ppId = init.json["id"].asString();
        REQUIRE(dbScalar("SELECT status FROM pinky_promises WHERE id = $1", ppId) ==
                "pending_b_confirm");

        REQUIRE(deleteProposal(a, pc.proposalId).status == k204NoContent);

        CHECK(dbScalar("SELECT status FROM pinky_promises WHERE id = $1", ppId) ==
              "pending_b_confirm");  // untouched
        CHECK(dbScalar("SELECT status FROM conversations WHERE id = $1", pc.conversationId) ==
              "expired");

        auto conf = confirmPp(b, ppId);
        CHECK(conf.status == k409Conflict);
        CHECK(conf.json["error"].asString() == "CONVERSATION_EXPIRED");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

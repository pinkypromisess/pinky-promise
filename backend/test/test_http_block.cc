#include <drogon/drogon_test.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "TestDb.h"
#include "TestHttpFixtures.h"
#include "TestHttpServer.h"

// Every DROGON_TEST here is HTTP-LEVEL: the request goes through the real
// Drogon router + the real auth::AuthFilter + a real Postgres DB (via the
// TestHttpServer harness), covering Module E.1's POST /v1/blocks and its
// cascade. The one precondition the real API can't itself construct
// (creating a confirmed PinkyPromise) is instead driven through the real
// initiate -> confirm HTTP flow, not seeded directly, so the cascade test
// exercises the real winning-conversation-is-pinky_promised state.

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
    auto body = buildProfileBody(sixPhotoUrls("block-" + s.userId));
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

HttpTestResponse blockUser(const TestSession &s, const std::string &blockedUserId)
{
    Json::Value body;
    body["blocked_user_id"] = blockedUserId;
    return sendTestRequest(s.baseUrl, Post, "/v1/blocks", s.token, &body);
}

std::string dbScalar(const std::string &sql, const std::string &arg)
{
    auto rows = testDbClient()->execSqlSync(sql, arg);
    return rows[0][0].as<std::string>();
}

int64_t dbBlockRowCount(const std::string &blockerUserId, const std::string &blockedUserId)
{
    auto rows = testDbClient()->execSqlSync(
        "SELECT count(*) AS c FROM blocks WHERE blocker_user_id = $1 AND blocked_user_id = $2",
        blockerUserId,
        blockedUserId);
    return rows[0]["c"].as<int64_t>();
}

// A (proposer) posts a proposal, B (interested) swipes `interested` -> an
// active conversation. Returns {proposalId, conversationId}.
struct PC
{
    std::string proposalId;
    std::string conversationId;
};

PC setUpActiveConversation(const TestSession &proposer, const TestSession &interested)
{
    PC pc;
    pc.proposalId = createProposalOverHttp(proposer);
    if (swipe(interested, pc.proposalId, "interested").status != k201Created)
    {
        throw std::runtime_error("interested swipe failed");
    }
    pc.conversationId = conversationIdFor(interested, pc.proposalId);
    return pc;
}

}  // namespace

// REQUIRED 1: successful block creation -- 201, correct shape.
DROGON_TEST(CreateBlockReturns201WithCorrectShape)
{
    try
    {
        auto a = setUpVerifiedSession();
        auto b = setUpVerifiedSession();

        auto resp = blockUser(a, b.userId);
        REQUIRE(resp.status == k201Created);
        CHECK(resp.json["blocker_user_id"].asString() == a.userId);
        CHECK(resp.json["blocked_user_id"].asString() == b.userId);
        CHECK(!resp.json["id"].asString().empty());
        CHECK(!resp.json["created_at"].asString().empty());
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 2: self-block rejected -- 400 CANNOT_BLOCK_SELF.
DROGON_TEST(SelfBlockRejected)
{
    try
    {
        auto a = setUpVerifiedSession();

        auto resp = blockUser(a, a.userId);
        CHECK(resp.status == k400BadRequest);
        CHECK(resp.json["error"].asString() == "CANNOT_BLOCK_SELF");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 3: blocking an unknown user rejected -- 400 USER_NOT_FOUND, for
// both a well-formed-but-nonexistent uuid and a malformed one.
DROGON_TEST(BlockingUnknownUserRejected)
{
    try
    {
        auto a = setUpVerifiedSession();

        auto respUnknown = blockUser(a, "00000000-0000-0000-0000-000000000000");
        CHECK(respUnknown.status == k400BadRequest);
        CHECK(respUnknown.json["error"].asString() == "USER_NOT_FOUND");

        auto respMalformed = blockUser(a, "not-a-uuid");
        CHECK(respMalformed.status == k400BadRequest);
        CHECK(respMalformed.json["error"].asString() == "USER_NOT_FOUND");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 4: re-blocking the same direction is idempotent -- 200, no
// duplicate row, and the second call succeeds cleanly (cascade not
// re-run in a way that errors).
DROGON_TEST(ReblockingSameDirectionIsIdempotent)
{
    try
    {
        auto a = setUpVerifiedSession();
        auto b = setUpVerifiedSession();

        auto first = blockUser(a, b.userId);
        REQUIRE(first.status == k201Created);
        const auto firstId = first.json["id"].asString();

        auto second = blockUser(a, b.userId);
        CHECK(second.status == k200OK);
        CHECK(second.json["id"].asString() == firstId);

        CHECK(dbBlockRowCount(a.userId, b.userId) == 1);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 5: an active Conversation between the pair -> `expired` after
// the block, and (reusing Module C's existing CONVERSATION_EXPIRED
// behaviour unchanged) posting a message to it now 409s.
DROGON_TEST(BlockCascadesActiveConversationToExpired)
{
    try
    {
        auto a = setUpVerifiedSession();
        auto b = setUpVerifiedSession();
        auto pc = setUpActiveConversation(a, b);
        REQUIRE(dbScalar("SELECT status FROM conversations WHERE id = $1", pc.conversationId) ==
                "active");

        REQUIRE(blockUser(a, b.userId).status == k201Created);

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

// REQUIRED 6: a confirmed PinkyPromise between the pair -> `cancelled`,
// and its conversation ('pinky_promised', not 'active') -> `expired` too
// (the IN ('active','pinky_promised') case). Uses the real
// initiate -> confirm HTTP flow so the winning conversation really is
// 'pinky_promised' going in, not seeded directly.
DROGON_TEST(BlockCascadesConfirmedPinkyPromiseToCancelled)
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
        REQUIRE(dbScalar("SELECT status FROM conversations WHERE id = $1", pc.conversationId) ==
                "pinky_promised");

        REQUIRE(blockUser(a, b.userId).status == k201Created);

        CHECK(dbScalar("SELECT status FROM pinky_promises WHERE id = $1", ppId) == "cancelled");
        CHECK(dbScalar("SELECT status FROM conversations WHERE id = $1", pc.conversationId) ==
              "expired");
        // proposals.status is deliberately left untouched by a block --
        // contrast with DELETE /proposals/{id}, which does cancel it.
        CHECK(dbScalar("SELECT status FROM proposals WHERE id = $1", pc.proposalId) ==
              "pinky_promised");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 7: directionality -- block A->B also closes a Conversation
// where B is the proposer and A is the interested user (i.e. the cascade
// checks both (proposer,interested) orderings against (blocker,blocked),
// not just one).
DROGON_TEST(BlockCascadeChecksBothConversationDirections)
{
    try
    {
        auto a = setUpVerifiedSession();
        auto b = setUpVerifiedSession();
        // B is the proposer here, A is interested -- the reverse of the
        // block's own (blocker=A, blocked=B) direction.
        auto pc = setUpActiveConversation(/*proposer=*/b, /*interested=*/a);
        REQUIRE(dbScalar("SELECT status FROM conversations WHERE id = $1", pc.conversationId) ==
                "active");

        REQUIRE(blockUser(a, b.userId).status == k201Created);

        CHECK(dbScalar("SELECT status FROM conversations WHERE id = $1", pc.conversationId) ==
              "expired");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 8: a Conversation NOT involving the blocked pair is untouched.
DROGON_TEST(BlockCascadeLeavesUnrelatedConversationUntouched)
{
    try
    {
        auto a = setUpVerifiedSession();
        auto b = setUpVerifiedSession();
        auto stranger = setUpVerifiedSession();

        auto blockedPair = setUpActiveConversation(a, b);
        auto unrelated = setUpActiveConversation(a, stranger);

        REQUIRE(blockUser(a, b.userId).status == k201Created);

        CHECK(dbScalar("SELECT status FROM conversations WHERE id = $1",
                        blockedPair.conversationId) == "expired");
        CHECK(dbScalar("SELECT status FROM conversations WHERE id = $1",
                        unrelated.conversationId) == "active");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

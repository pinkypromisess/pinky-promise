#include <drogon/drogon_test.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "TestDb.h"
#include "TestHttpFixtures.h"
#include "TestHttpServer.h"

// Every DROGON_TEST in this file is HTTP-LEVEL: the request goes through
// the real Drogon router + the real auth::AuthFilter + a real Postgres DB
// (via the TestHttpServer harness). The "already at 3" cap state is seeded
// by inserting confirmed pinky_promises rows joined to proposals with a
// future event_time directly through the DB harness -- no real waiting.
//
// The cap arithmetic is simple enough that it is exercised only at this
// (HTTP) layer with seeded rows; there is no separate pure-logic file.

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
    auto body = buildProfileBody(sixPhotoUrls("pp-" + s.userId));
    auto put = sendTestRequest(s.baseUrl, Put, "/v1/profile", s.token, &body);
    if (put.status != k200OK)
    {
        throw std::runtime_error("PUT /v1/profile -> " + std::to_string(put.status));
    }
    auto decision = verifyOverHttp(s);
    if (decision != "pass")
    {
        throw std::runtime_error("verifyOverHttp -> " + decision);
    }
    return s;
}

Json::Value validProposalBody()
{
    Json::Value body;
    body["activity_text"] = "grab coffee";
    body["event_time"] = "2026-09-01T18:00:00Z";
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

HttpTestResponse postMessage(const TestSession &s, const std::string &cid,
                              const std::string &type, const std::string &content)
{
    Json::Value b;
    b["type"] = type;
    b["content"] = content;
    return sendTestRequest(s.baseUrl, Post, "/v1/conversations/" + cid + "/messages", s.token, &b);
}

HttpTestResponse initiatePp(const TestSession &s, const std::string &conversationId)
{
    return sendTestRequest(
        s.baseUrl, Post, "/v1/conversations/" + conversationId + "/pinky-promise", s.token);
}

HttpTestResponse confirmPp(const TestSession &s, const std::string &pinkyPromiseId)
{
    return sendTestRequest(
        s.baseUrl, Post, "/v1/pinky-promises/" + pinkyPromiseId + "/confirm", s.token);
}

// The id of `session`'s conversation for `proposalId` (each fresh B has
// exactly one, but filter anyway).
std::string conversationIdFor(const TestSession &session, const std::string &proposalId)
{
    auto list = sendTestRequest(session.baseUrl, Get, "/v1/conversations", session.token);
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

struct Fixture
{
    TestSession a;  // proposer / initiator
    TestSession b;  // interested / confirmer
    std::string proposalId;
    std::string conversationId;
};

Fixture setUpConversation()
{
    Fixture f;
    f.a = setUpVerifiedSession();
    f.b = setUpVerifiedSession();
    f.proposalId = createProposalOverHttp(f.a);
    if (swipe(f.b, f.proposalId, "interested").status != k201Created)
    {
        throw std::runtime_error("interested swipe failed");
    }
    f.conversationId = conversationIdFor(f.b, f.proposalId);
    return f;
}

Json::Value getConversation(const TestSession &s, const std::string &cid)
{
    auto resp = sendTestRequest(s.baseUrl, Get, "/v1/conversations/" + cid, s.token);
    if (resp.status != k200OK)
    {
        throw std::runtime_error("GET /v1/conversations/{id} -> " + std::to_string(resp.status));
    }
    return resp.json;
}

std::string dbScalar(const std::string &sql, const std::string &arg)
{
    auto rows = testDbClient()->execSqlSync(sql, arg);
    return rows[0][0].as<std::string>();
}

// Inserts `count` confirmed pinky_promises in which `userId` participates
// (as user_a), each on its own fresh proposal + conversation. event_time
// is +10 days when `future`, else -10 days. Fully parameterized -- the day
// offset is a bound int passed to make_interval().
void dbSeedConfirmedPinkyPromises(const std::string &userId, int count, bool future)
{
    auto db = testDbClient();
    const int dayOffset = future ? 10 : -10;
    for (int i = 0; i < count; ++i)
    {
        auto otherId = createTestUser(db);
        auto proposalId = db->execSqlSync(
            "INSERT INTO proposals (creator_user_id, activity_text, event_time, location_lat, "
            "location_lng, location_address, payment_type, looking_for_text) "
            "VALUES ($1, 'seed', now() + make_interval(days => $2), 0, 0, 'seed addr', 'split', "
            "'seed') RETURNING id",
            userId,
            dayOffset)[0]["id"]
                             .as<std::string>();
        auto conversationId = db->execSqlSync(
            "INSERT INTO conversations (proposal_id, proposer_user_id, interested_user_id, "
            "last_activity_at) VALUES ($1, $2, $3, now()) RETURNING id",
            proposalId,
            userId,
            otherId)[0]["id"]
                                 .as<std::string>();
        db->execSqlSync(
            "INSERT INTO pinky_promises (proposal_id, conversation_id, user_a_id, user_b_id, "
            "status, confirmed_at) VALUES ($1, $2, $3, $4, 'confirmed', now())",
            proposalId,
            conversationId,
            userId,
            otherId);
    }
}

}  // namespace

// REQUIRED (a): pending_b_confirm -> confirmed transition, with all four
// writes (PP, proposal, winning conversation) landing.
DROGON_TEST(PendingToConfirmedTransition)
{
    try
    {
        auto f = setUpConversation();

        auto init = initiatePp(f.a, f.conversationId);
        REQUIRE(init.status == k201Created);
        CHECK(init.json["status"].asString() == "pending_b_confirm");
        CHECK(init.json["confirmed_at"].isNull());
        CHECK(init.json["user_a_id"].asString() == f.a.userId);
        CHECK(init.json["user_b_id"].asString() == f.b.userId);
        CHECK(init.json["proposal_id"].asString() == f.proposalId);
        CHECK(init.json["conversation_id"].asString() == f.conversationId);
        const auto ppId = init.json["id"].asString();

        auto conf = confirmPp(f.b, ppId);
        REQUIRE(conf.status == k200OK);
        CHECK(conf.json["status"].asString() == "confirmed");
        CHECK(!conf.json["confirmed_at"].isNull());

        CHECK(dbScalar("SELECT status FROM pinky_promises WHERE id = $1", ppId) == "confirmed");
        CHECK(dbScalar("SELECT status FROM proposals WHERE id = $1", f.proposalId) ==
              "pinky_promised");
        CHECK(dbScalar("SELECT status FROM conversations WHERE id = $1", f.conversationId) ==
              "pinky_promised");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED (f): once confirmed, the winning conversation's computed
// expires_at is null; while still pending it is NOT null (the decay clock
// keeps running during pending_b_confirm, per CUJ #4).
DROGON_TEST(WinningConversationStopsExpiringOnlyAfterConfirm)
{
    try
    {
        auto f = setUpConversation();

        auto init = initiatePp(f.a, f.conversationId);
        REQUIRE(init.status == k201Created);
        CHECK(!getConversation(f.a, f.conversationId)["expires_at"].isNull());  // still decaying

        auto conf = confirmPp(f.b, init.json["id"].asString());
        REQUIRE(conf.status == k200OK);
        CHECK(getConversation(f.a, f.conversationId)["expires_at"].isNull());  // un-timed now
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED (b): confirm rejected when the INITIATOR (user A) already has 3
// confirmed upcoming Pinky Promises. The PP stays pending, proposal stays
// active.
DROGON_TEST(CapRejectionWhenInitiatorAtThree)
{
    try
    {
        auto f = setUpConversation();
        dbSeedConfirmedPinkyPromises(f.a.userId, 3, /*future=*/true);

        auto init = initiatePp(f.a, f.conversationId);
        REQUIRE(init.status == k201Created);  // cap NOT checked at initiate
        const auto ppId = init.json["id"].asString();

        auto conf = confirmPp(f.b, ppId);
        REQUIRE(conf.status == k409Conflict);
        CHECK(conf.json["error"].asString() == "PINKY_PROMISE_CAP_REACHED");

        CHECK(dbScalar("SELECT status FROM pinky_promises WHERE id = $1", ppId) ==
              "pending_b_confirm");
        CHECK(dbScalar("SELECT status FROM proposals WHERE id = $1", f.proposalId) == "active");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED (c): confirm rejected when the CONFIRMER (user B) already has 3
// confirmed upcoming Pinky Promises.
DROGON_TEST(CapRejectionWhenConfirmerAtThree)
{
    try
    {
        auto f = setUpConversation();
        dbSeedConfirmedPinkyPromises(f.b.userId, 3, /*future=*/true);

        auto init = initiatePp(f.a, f.conversationId);
        REQUIRE(init.status == k201Created);
        const auto ppId = init.json["id"].asString();

        auto conf = confirmPp(f.b, ppId);
        REQUIRE(conf.status == k409Conflict);
        CHECK(conf.json["error"].asString() == "PINKY_PROMISE_CAP_REACHED");

        CHECK(dbScalar("SELECT status FROM pinky_promises WHERE id = $1", ppId) ==
              "pending_b_confirm");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// CHECKS: only CONFIRMED PPs whose proposal event_time is in the FUTURE
// count toward the cap -- 3 seeded PPs with a past event_time do not block
// the confirm.
DROGON_TEST(CapIgnoresPastEventPinkyPromises)
{
    try
    {
        auto f = setUpConversation();
        dbSeedConfirmedPinkyPromises(f.a.userId, 3, /*future=*/false);  // all in the past
        dbSeedConfirmedPinkyPromises(f.b.userId, 3, /*future=*/false);

        auto init = initiatePp(f.a, f.conversationId);
        REQUIRE(init.status == k201Created);

        auto conf = confirmPp(f.b, init.json["id"].asString());
        REQUIRE(conf.status == k200OK);
        CHECK(conf.json["status"].asString() == "confirmed");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// CHECKS: with exactly 2 confirmed upcoming PPs, a 3rd confirm is still
// allowed (boundary: reject only at >= 3).
DROGON_TEST(CapAllowsThirdConfirm)
{
    try
    {
        auto f = setUpConversation();
        dbSeedConfirmedPinkyPromises(f.a.userId, 2, /*future=*/true);
        dbSeedConfirmedPinkyPromises(f.b.userId, 2, /*future=*/true);

        auto init = initiatePp(f.a, f.conversationId);
        REQUIRE(init.status == k201Created);

        auto conf = confirmPp(f.b, init.json["id"].asString());
        REQUIRE(conf.status == k200OK);
        CHECK(conf.json["status"].asString() == "confirmed");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED (d): sibling Conversation closure. A proposal with two active
// conversations; one confirms; the other flips to `expired` and can no
// longer accept messages, while the winning one still can.
DROGON_TEST(SiblingConversationsCloseOnConfirm)
{
    try
    {
        auto a = setUpVerifiedSession();
        auto b1 = setUpVerifiedSession();
        auto b2 = setUpVerifiedSession();
        auto proposalId = createProposalOverHttp(a);
        REQUIRE(swipe(b1, proposalId, "interested").status == k201Created);
        REQUIRE(swipe(b2, proposalId, "interested").status == k201Created);
        const auto conv1 = conversationIdFor(b1, proposalId);
        const auto conv2 = conversationIdFor(b2, proposalId);

        auto init = initiatePp(a, conv1);
        REQUIRE(init.status == k201Created);
        auto conf = confirmPp(b1, init.json["id"].asString());
        REQUIRE(conf.status == k200OK);

        CHECK(dbScalar("SELECT status FROM conversations WHERE id = $1", conv1) ==
              "pinky_promised");
        CHECK(dbScalar("SELECT status FROM conversations WHERE id = $1", conv2) == "expired");

        // The closed sibling rejects new messages; the winning one still accepts.
        CHECK(postMessage(b2, conv2, "text", "hello?").status == k409Conflict);
        CHECK(postMessage(a, conv1, "text", "see you there").status == k201Created);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED (e), initiate side: only the proposer may initiate.
DROGON_TEST(OnlyProposerCanInitiate)
{
    try
    {
        auto f = setUpConversation();
        auto stranger = setUpVerifiedSession();

        auto byB = initiatePp(f.b, f.conversationId);
        REQUIRE(byB.status == k403Forbidden);
        CHECK(byB.json["error"].asString() == "NOT_INITIATOR");

        auto byStranger = initiatePp(stranger, f.conversationId);
        REQUIRE(byStranger.status == k403Forbidden);
        CHECK(byStranger.json["error"].asString() == "NOT_A_PARTICIPANT");

        auto unknown = initiatePp(f.a, "123e4567-e89b-12d3-a456-426614174000");
        CHECK(unknown.status == k404NotFound);
        CHECK(unknown.json["error"].asString() == "CONVERSATION_NOT_FOUND");
        CHECK(initiatePp(f.a, "not-a-uuid").status == k404NotFound);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED (e), confirm side: only user_b may confirm; unknown/malformed
// PP id is 404.
DROGON_TEST(OnlyInvitedUserCanConfirm)
{
    try
    {
        auto f = setUpConversation();
        auto stranger = setUpVerifiedSession();
        auto init = initiatePp(f.a, f.conversationId);
        REQUIRE(init.status == k201Created);
        const auto ppId = init.json["id"].asString();

        auto byA = confirmPp(f.a, ppId);
        REQUIRE(byA.status == k403Forbidden);
        CHECK(byA.json["error"].asString() == "NOT_CONFIRMER");

        auto byStranger = confirmPp(stranger, ppId);
        REQUIRE(byStranger.status == k403Forbidden);
        CHECK(byStranger.json["error"].asString() == "NOT_CONFIRMER");

        auto unknown = confirmPp(f.b, "123e4567-e89b-12d3-a456-426614174000");
        CHECK(unknown.status == k404NotFound);
        CHECK(unknown.json["error"].asString() == "PINKY_PROMISE_NOT_FOUND");
        CHECK(confirmPp(f.b, "not-a-uuid").status == k404NotFound);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED (e): confirming a PinkyPromise that is not pending_b_confirm is 409.
DROGON_TEST(ConfirmingNonPendingIs409)
{
    try
    {
        auto f = setUpConversation();
        auto init = initiatePp(f.a, f.conversationId);
        REQUIRE(init.status == k201Created);
        const auto ppId = init.json["id"].asString();

        REQUIRE(confirmPp(f.b, ppId).status == k200OK);

        auto again = confirmPp(f.b, ppId);
        REQUIRE(again.status == k409Conflict);
        CHECK(again.json["error"].asString() == "NOT_PENDING_CONFIRM");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// CHECKS: a second initiate on the same conversation (while one is still
// pending) is 409 PINKY_PROMISE_EXISTS; and once confirmed, a fresh
// initiate is 409 ALREADY_PINKY_PROMISED.
DROGON_TEST(DoubleInitiateAndInitiateAfterConfirmAre409)
{
    try
    {
        auto f = setUpConversation();

        auto first = initiatePp(f.a, f.conversationId);
        REQUIRE(first.status == k201Created);

        auto dup = initiatePp(f.a, f.conversationId);
        REQUIRE(dup.status == k409Conflict);
        CHECK(dup.json["error"].asString() == "PINKY_PROMISE_EXISTS");

        REQUIRE(confirmPp(f.b, first.json["id"].asString()).status == k200OK);

        auto afterConfirm = initiatePp(f.a, f.conversationId);
        REQUIRE(afterConfirm.status == k409Conflict);
        CHECK(afterConfirm.json["error"].asString() == "ALREADY_PINKY_PROMISED");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// CHECKS: initiating on a conversation whose computed status is expired is
// 409 CONVERSATION_EXPIRED.
DROGON_TEST(InitiateOnExpiredConversationIs409)
{
    try
    {
        auto f = setUpConversation();
        // 4 days old, no messages -> computed expiry (created + 3d) is past.
        testDbClient()->execSqlSync(
            "UPDATE conversations SET created_at = now() - interval '4 days' WHERE id = $1",
            f.conversationId);

        auto init = initiatePp(f.a, f.conversationId);
        REQUIRE(init.status == k409Conflict);
        CHECK(init.json["error"].asString() == "CONVERSATION_EXPIRED");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// CHECKS: both PinkyPromise routes sit behind auth::AuthFilter.
DROGON_TEST(PinkyPromiseRoutesRequireAuth)
{
    try
    {
        auto baseUrl = ensureTestServerRunning();
        const std::string uuid = "123e4567-e89b-12d3-a456-426614174000";

        CHECK(sendTestRequest(baseUrl, Post, "/v1/conversations/" + uuid + "/pinky-promise", "")
                  .status == k401Unauthorized);
        CHECK(sendTestRequest(baseUrl, Post, "/v1/pinky-promises/" + uuid + "/confirm", "")
                  .status == k401Unauthorized);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

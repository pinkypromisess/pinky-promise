#include <drogon/drogon_test.h>

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/storage/TimeUtils.h"
#include "TestDb.h"
#include "TestHttpFixtures.h"
#include "TestHttpServer.h"

// Every DROGON_TEST in this file is HTTP-LEVEL: the request goes through
// the real Drogon router + the real auth::AuthFilter + a real Postgres DB
// (via the TestHttpServer harness). Boundary timestamps are seeded by
// backdating rows directly through the DB harness -- no real sleeps. The
// pure expiry-formula coverage is in test_conversation_expiry.cc.

using namespace test_support;
using namespace drogon;

namespace
{
long long nowEpoch()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string isoFromEpoch(long long epoch)
{
    return storage::formatIso8601Utc(
        std::chrono::system_clock::time_point(std::chrono::seconds(epoch)));
}

constexpr long long H = 3600;
constexpr long long D = 86400;

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
    auto body = buildProfileBody(sixPhotoUrls("conv-" + s.userId));
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

Json::Value messageBody(const std::string &type, const std::string &content)
{
    Json::Value b;
    b["type"] = type;
    b["content"] = content;
    return b;
}

HttpTestResponse postMessage(const TestSession &s, const std::string &cid,
                              const std::string &type, const std::string &content)
{
    auto b = messageBody(type, content);
    return sendTestRequest(s.baseUrl, Post, "/v1/conversations/" + cid + "/messages", s.token, &b);
}

struct ConversationFixture
{
    TestSession a;  // proposer / proposal creator
    TestSession b;  // interested / swiper
    std::string proposalId;
    std::string conversationId;
};

// Verified A posts a proposal; verified B swipes `interested`, which opens
// the conversation; returns everything wired together.
ConversationFixture setUpConversation()
{
    ConversationFixture f;
    f.a = setUpVerifiedSession();
    f.b = setUpVerifiedSession();
    f.proposalId = createProposalOverHttp(f.a);
    auto sw = swipe(f.b, f.proposalId, "interested");
    if (sw.status != k201Created)
    {
        throw std::runtime_error("interested swipe -> " + std::to_string(sw.status));
    }
    auto list = sendTestRequest(f.b.baseUrl, Get, "/v1/conversations", f.b.token);
    if (list.status != k200OK || list.json["conversations"].empty())
    {
        throw std::runtime_error("interested swipe did not open a conversation");
    }
    f.conversationId = list.json["conversations"][0]["id"].asString();
    return f;
}

// NOTE on timestamp seeding: epochs are passed as ISO-8601 strings bound
// as `$n::timestamptz`, never as integers into `to_timestamp($n)`. drogon
// binds a `long long` in binary int8 form, but `to_timestamp` makes
// Postgres infer float8 for that slot, so the bytes get read as a
// denormal ~= 0 (every seeded time silently became 1970). A text-bound
// ISO string with an explicit `::timestamptz` cast is unambiguous and
// still fully parameterized.
void dbSetCreatedAt(const std::string &cid, long long epoch)
{
    testDbClient()->execSqlSync(
        "UPDATE conversations SET created_at = $2::timestamptz WHERE id = $1",
        cid,
        isoFromEpoch(epoch));
}

void dbSetStatus(const std::string &cid, const std::string &status)
{
    testDbClient()->execSqlSync(
        "UPDATE conversations SET status = $2 WHERE id = $1", cid, status);
}

void dbSeedMessage(const std::string &cid, const std::string &senderUserId, long long epoch)
{
    testDbClient()->execSqlSync(
        "INSERT INTO messages (conversation_id, sender_user_id, type, content_or_url, created_at) "
        "VALUES ($1, $2, 'text', 'seed', $3::timestamptz)",
        cid,
        senderUserId,
        isoFromEpoch(epoch));
}

// `count` alternating messages at startEpoch + g minutes (g = 1..count):
// odd g -> B, even g -> A.
void dbSeedAlternatingMessages(const std::string &cid, const std::string &aId,
                                const std::string &bId, long long startEpoch, int count)
{
    testDbClient()->execSqlSync(
        "INSERT INTO messages (conversation_id, sender_user_id, type, content_or_url, created_at) "
        "SELECT $1, CASE WHEN g % 2 = 1 THEN $2::uuid ELSE $3::uuid END, 'text', 'seed', "
        "       $4::timestamptz + (g * interval '1 minute') "
        "FROM generate_series(1, $5) AS g",
        cid,
        bId,
        aId,
        isoFromEpoch(startEpoch),
        count);
}

}  // namespace

// REQUIRED (a): an `interested` swipe opens a conversation, visible to
// both A and B, with the expected shape.
DROGON_TEST(InterestedSwipeOpensConversationVisibleToBothParties)
{
    try
    {
        auto f = setUpConversation();

        auto asB = sendTestRequest(f.b.baseUrl, Get, "/v1/conversations", f.b.token);
        REQUIRE(asB.status == k200OK);
        REQUIRE(asB.json["conversations"].size() == 1u);
        const auto conv = asB.json["conversations"][0];
        CHECK(conv["proposal_id"].asString() == f.proposalId);
        CHECK(conv["proposer_user_id"].asString() == f.a.userId);
        CHECK(conv["interested_user_id"].asString() == f.b.userId);
        CHECK(conv["status"].asString() == "active");
        CHECK(conv["last_sender_id"].isNull());
        CHECK(!conv["expires_at"].asString().empty());  // active -> computed, non-null

        auto asA = sendTestRequest(f.a.baseUrl, Get, "/v1/conversations", f.a.token);
        REQUIRE(asA.status == k200OK);
        REQUIRE(asA.json["conversations"].size() == 1u);
        CHECK(asA.json["conversations"][0]["id"].asString() == f.conversationId);

        auto rows = testDbClient()->execSqlSync(
            "SELECT count(*) AS c FROM conversations WHERE proposal_id = $1 AND "
            "interested_user_id = $2",
            f.proposalId,
            f.b.userId);
        CHECK(rows[0]["c"].as<int64_t>() == 1);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED (a): a `pass` swipe opens no conversation.
DROGON_TEST(PassSwipeOpensNoConversation)
{
    try
    {
        auto a = setUpVerifiedSession();
        auto b = setUpVerifiedSession();
        auto pid = createProposalOverHttp(a);

        REQUIRE(swipe(b, pid, "pass").status == k201Created);

        auto list = sendTestRequest(b.baseUrl, Get, "/v1/conversations", b.token);
        REQUIRE(list.status == k200OK);
        CHECK(list.json["conversations"].empty());

        auto rows = testDbClient()->execSqlSync(
            "SELECT count(*) AS c FROM conversations WHERE proposal_id = $1", pid);
        CHECK(rows[0]["c"].as<int64_t>() == 0);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED (b): A's first reply sets expires_at to that message + 12h
// (within a 2s wall-clock tolerance -- `aFirst` is now()-relative).
DROGON_TEST(ExpiresAtIsTwelveHoursAfterAFirstReplySeeded)
{
    try
    {
        auto f = setUpConversation();
        const long long created = nowEpoch() - H;
        const long long aFirst = nowEpoch() - H / 2;
        dbSetCreatedAt(f.conversationId, created);
        dbSeedMessage(f.conversationId, f.a.userId, aFirst);

        auto resp = sendTestRequest(
            f.b.baseUrl, Get, "/v1/conversations/" + f.conversationId, f.b.token);
        REQUIRE(resp.status == k200OK);
        CHECK(timestampSkewSeconds(isoFromEpoch(aFirst + 12 * H),
                                    resp.json["expires_at"].asString()) <= 2);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED (b), over the real POST path: posting A's first reply moves
// expires_at to ~12h after that message (checked with a small tolerance
// around wall-clock "now").
DROGON_TEST(PostingAFirstReplySetsTwelveHourBaseOverHttp)
{
    try
    {
        auto f = setUpConversation();
        const long long before = nowEpoch();
        auto post = postMessage(f.a, f.conversationId, "text", "hey there");
        REQUIRE(post.status == k201Created);
        const long long after = nowEpoch();

        auto resp = sendTestRequest(
            f.a.baseUrl, Get, "/v1/conversations/" + f.conversationId, f.a.token);
        REQUIRE(resp.status == k200OK);
        const long long expEpoch = parseIso8601UtcToEpoch(resp.json["expires_at"].asString());
        // The message's created_at is a server now() between `before` and
        // `after`; +/-2s absorbs CI scheduler jitter and the FLOOR() on the
        // epoch read.
        CHECK(expEpoch >= before + 12 * H - 2);
        CHECK(expEpoch <= after + 12 * H + 2);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED (c): +1 minute per same-sender repeat after A's first reply.
DROGON_TEST(ExpiresAtSameSenderRepeatAddsOneMinuteEachSeeded)
{
    try
    {
        auto f = setUpConversation();
        const long long created = nowEpoch() - 2 * H;
        const long long t = nowEpoch() - H;
        dbSetCreatedAt(f.conversationId, created);
        dbSeedMessage(f.conversationId, f.a.userId, t);        // A's first reply
        dbSeedMessage(f.conversationId, f.a.userId, t + 60);   // same sender -> +1min
        dbSeedMessage(f.conversationId, f.a.userId, t + 120);  // same sender -> +1min

        auto resp = sendTestRequest(
            f.b.baseUrl, Get, "/v1/conversations/" + f.conversationId, f.b.token);
        REQUIRE(resp.status == k200OK);
        CHECK(timestampSkewSeconds(isoFromEpoch(t + 12 * H + 120),
                                    resp.json["expires_at"].asString()) <= 2);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED (d): +2 hours per first-reply-after-the-other-side.
DROGON_TEST(ExpiresAtOtherSideReplyAddsTwoHoursEachSeeded)
{
    try
    {
        auto f = setUpConversation();
        const long long created = nowEpoch() - 2 * H;
        const long long t = nowEpoch() - H;
        dbSetCreatedAt(f.conversationId, created);
        dbSeedMessage(f.conversationId, f.a.userId, t);        // A's first reply
        dbSeedMessage(f.conversationId, f.b.userId, t + 60);   // switch A->B -> +2h
        dbSeedMessage(f.conversationId, f.a.userId, t + 120);  // switch B->A -> +2h

        auto resp = sendTestRequest(
            f.b.baseUrl, Get, "/v1/conversations/" + f.conversationId, f.b.token);
        REQUIRE(resp.status == k200OK);
        CHECK(timestampSkewSeconds(isoFromEpoch(t + 12 * H + 4 * H),
                                    resp.json["expires_at"].asString()) <= 2);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED (e), path 1: A never replied -> expires_at == created_at + 3 days
// (messages B sent into the void don't change it).
DROGON_TEST(ExpiresAtThreeDayCapWhenANeverRepliedSeeded)
{
    try
    {
        auto f = setUpConversation();
        const long long created = nowEpoch() - H;
        dbSetCreatedAt(f.conversationId, created);
        dbSeedMessage(f.conversationId, f.b.userId, created + 100);
        dbSeedMessage(f.conversationId, f.b.userId, created + 200);

        auto resp = sendTestRequest(
            f.b.baseUrl, Get, "/v1/conversations/" + f.conversationId, f.b.token);
        REQUIRE(resp.status == k200OK);
        CHECK(timestampSkewSeconds(isoFromEpoch(created + 3 * D),
                                    resp.json["expires_at"].asString()) <= 2);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED (e), path 2: heavy back-and-forth activity is still clamped to
// created_at + 3 days.
DROGON_TEST(ExpiresAtThreeDayCapDespiteHeavyActivitySeeded)
{
    try
    {
        auto f = setUpConversation();
        const long long created = nowEpoch() - H;
        const long long aFirst = created + 1;
        dbSetCreatedAt(f.conversationId, created);
        dbSeedMessage(f.conversationId, f.a.userId, aFirst);  // A's first reply
        // 80 alternating messages -> ~80 * 2h of bonus, far past 3 days.
        dbSeedAlternatingMessages(f.conversationId, f.a.userId, f.b.userId, aFirst, 80);

        auto resp = sendTestRequest(
            f.b.baseUrl, Get, "/v1/conversations/" + f.conversationId, f.b.token);
        REQUIRE(resp.status == k200OK);
        CHECK(timestampSkewSeconds(isoFromEpoch(created + 3 * D),
                                    resp.json["expires_at"].asString()) <= 2);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED (f): posting a message returns 201 with the created message and
// bumps last_sender_id / last_activity_at; both participants can post.
DROGON_TEST(PostMessageUpdatesLastSenderAndReturnsMessage)
{
    try
    {
        auto f = setUpConversation();

        auto p1 = postMessage(f.a, f.conversationId, "text", "hi from A");
        REQUIRE(p1.status == k201Created);
        CHECK(p1.json["conversation_id"].asString() == f.conversationId);
        CHECK(p1.json["sender_user_id"].asString() == f.a.userId);
        CHECK(p1.json["type"].asString() == "text");
        CHECK(p1.json["content"].asString() == "hi from A");
        CHECK(!p1.json["id"].asString().empty());
        CHECK(!p1.json["created_at"].asString().empty());

        auto g1 = sendTestRequest(
            f.b.baseUrl, Get, "/v1/conversations/" + f.conversationId, f.b.token);
        REQUIRE(g1.status == k200OK);
        CHECK(g1.json["last_sender_id"].asString() == f.a.userId);
        CHECK(!g1.json["last_activity_at"].asString().empty());

        auto p2 = postMessage(f.b, f.conversationId, "voice", "https://gcs/voice/1.m4a");
        REQUIRE(p2.status == k201Created);
        CHECK(p2.json["type"].asString() == "voice");

        auto g2 = sendTestRequest(
            f.a.baseUrl, Get, "/v1/conversations/" + f.conversationId, f.a.token);
        REQUIRE(g2.status == k200OK);
        CHECK(g2.json["last_sender_id"].asString() == f.b.userId);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED (f): a non-participant can neither read nor post to the
// conversation (403), and doesn't see it in their own list.
DROGON_TEST(NonParticipantCannotReadOrPostToConversation)
{
    try
    {
        auto f = setUpConversation();
        auto stranger = setUpVerifiedSession();

        auto get = sendTestRequest(
            stranger.baseUrl, Get, "/v1/conversations/" + f.conversationId, stranger.token);
        REQUIRE(get.status == k403Forbidden);
        CHECK(get.json["error"].asString() == "NOT_A_PARTICIPANT");

        auto post = postMessage(stranger, f.conversationId, "text", "let me in");
        REQUIRE(post.status == k403Forbidden);
        CHECK(post.json["error"].asString() == "NOT_A_PARTICIPANT");

        auto list = sendTestRequest(stranger.baseUrl, Get, "/v1/conversations", stranger.token);
        REQUIRE(list.status == k200OK);
        CHECK(list.json["conversations"].empty());
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED (f): posting to a conversation whose COMPUTED status is expired
// is rejected 409; flipping it to pinky_promised makes messages flow again
// and drops expires_at to null.
DROGON_TEST(PostMessageToExpiredRejected409ButPinkyPromisedStillAccepts)
{
    try
    {
        auto f = setUpConversation();
        // 4 days old, no messages -> computed expiry (created + 3d) is in the past.
        dbSetCreatedAt(f.conversationId, nowEpoch() - 4 * D);

        auto expired = postMessage(f.a, f.conversationId, "text", "still there?");
        REQUIRE(expired.status == k409Conflict);
        CHECK(expired.json["error"].asString() == "CONVERSATION_EXPIRED");

        // GET is unaffected by expiry -- only POST is blocked.
        auto get = sendTestRequest(
            f.a.baseUrl, Get, "/v1/conversations/" + f.conversationId, f.a.token);
        REQUIRE(get.status == k200OK);

        dbSetStatus(f.conversationId, "pinky_promised");
        auto ok = postMessage(f.b, f.conversationId, "text", "we are on");
        REQUIRE(ok.status == k201Created);

        auto get2 = sendTestRequest(
            f.b.baseUrl, Get, "/v1/conversations/" + f.conversationId, f.b.token);
        REQUIRE(get2.status == k200OK);
        CHECK(get2.json["expires_at"].isNull());
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// CHECKS: unknown and malformed conversation ids are 404 on both GET-one
// and POST-message.
DROGON_TEST(UnknownOrMalformedConversationIdReturns404)
{
    try
    {
        auto s = setUpVerifiedSession();
        const std::string unknown = "123e4567-e89b-12d3-a456-426614174000";

        auto g1 = sendTestRequest(s.baseUrl, Get, "/v1/conversations/" + unknown, s.token);
        CHECK(g1.status == k404NotFound);
        CHECK(g1.json["error"].asString() == "CONVERSATION_NOT_FOUND");

        auto g2 = sendTestRequest(s.baseUrl, Get, "/v1/conversations/not-a-uuid", s.token);
        CHECK(g2.status == k404NotFound);

        CHECK(postMessage(s, unknown, "text", "hello?").status == k404NotFound);
        CHECK(postMessage(s, "not-a-uuid", "text", "hello?").status == k404NotFound);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// CHECKS: message body validation -- bad type, blank content, missing body.
DROGON_TEST(PostMessageBodyValidationRejects400)
{
    try
    {
        auto f = setUpConversation();

        auto badType = postMessage(f.a, f.conversationId, "gif", "x");
        REQUIRE(badType.status == k400BadRequest);
        CHECK(badType.json["error"].asString() == "MESSAGE_TYPE_INVALID");

        auto blank = postMessage(f.a, f.conversationId, "text", "   ");
        REQUIRE(blank.status == k400BadRequest);
        CHECK(blank.json["error"].asString() == "MESSAGE_CONTENT_REQUIRED");

        auto noBody = sendTestRequest(
            f.a.baseUrl, Post, "/v1/conversations/" + f.conversationId + "/messages", f.a.token);
        CHECK(noBody.status == k400BadRequest);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// CHECKS: all three conversation routes sit behind auth::AuthFilter.
DROGON_TEST(ConversationRoutesRequireAuth)
{
    try
    {
        auto baseUrl = ensureTestServerRunning();
        const std::string uuid = "123e4567-e89b-12d3-a456-426614174000";

        CHECK(sendTestRequest(baseUrl, Get, "/v1/conversations", "").status == k401Unauthorized);
        CHECK(sendTestRequest(baseUrl, Get, "/v1/conversations/" + uuid, "").status ==
              k401Unauthorized);
        auto body = messageBody("text", "hi");
        CHECK(sendTestRequest(baseUrl, Post, "/v1/conversations/" + uuid + "/messages", "", &body)
                  .status == k401Unauthorized);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// CHECKS: GET /v1/conversations lists most-recent-activity first -- a fresh
// message to the older conversation moves it to the front.
DROGON_TEST(ListConversationsOrderedByLastActivityDesc)
{
    try
    {
        auto a = setUpVerifiedSession();
        auto b = setUpVerifiedSession();
        auto p1 = createProposalOverHttp(a);
        auto p2 = createProposalOverHttp(a);
        REQUIRE(swipe(b, p1, "interested").status == k201Created);
        REQUIRE(swipe(b, p2, "interested").status == k201Created);

        auto list0 = sendTestRequest(b.baseUrl, Get, "/v1/conversations", b.token);
        REQUIRE(list0.status == k200OK);
        REQUIRE(list0.json["conversations"].size() == 2u);

        std::string convP1;
        for (const auto &c : list0.json["conversations"])
        {
            if (c["proposal_id"].asString() == p1)
            {
                convP1 = c["id"].asString();
            }
        }
        REQUIRE(!convP1.empty());

        REQUIRE(postMessage(a, convP1, "text", "bump").status == k201Created);

        auto list1 = sendTestRequest(b.baseUrl, Get, "/v1/conversations", b.token);
        REQUIRE(list1.status == k200OK);
        REQUIRE(list1.json["conversations"].size() == 2u);
        CHECK(list1.json["conversations"][0]["id"].asString() == convP1);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// CHECKS: GET /v1/conversations/{id} returns the single conversation to
// either participant, with a computed expires_at.
DROGON_TEST(GetConversationByIdAsEitherParticipant)
{
    try
    {
        auto f = setUpConversation();

        auto asA = sendTestRequest(
            f.a.baseUrl, Get, "/v1/conversations/" + f.conversationId, f.a.token);
        REQUIRE(asA.status == k200OK);
        CHECK(asA.json["id"].asString() == f.conversationId);
        CHECK(asA.json["proposer_user_id"].asString() == f.a.userId);
        CHECK(!asA.json["expires_at"].asString().empty());

        auto asB = sendTestRequest(
            f.b.baseUrl, Get, "/v1/conversations/" + f.conversationId, f.b.token);
        REQUIRE(asB.status == k200OK);
        CHECK(asB.json["id"].asString() == f.conversationId);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// --- GET /v1/conversations/{id}/messages (manager addition — Frontend
// Module 3 has no way to render message history without this; C.2
// shipped POST but never a way to read it back). ---

// CHECKS: messages come back oldest-first, visible to either participant.
DROGON_TEST(ListMessagesReturnsOrderedHistoryToEitherParticipant)
{
    try
    {
        auto f = setUpConversation();
        REQUIRE(postMessage(f.a, f.conversationId, "text", "first").status == k201Created);
        REQUIRE(postMessage(f.b, f.conversationId, "text", "second").status == k201Created);
        REQUIRE(postMessage(f.a, f.conversationId, "voice", "https://gcs/voice/3.m4a").status ==
                k201Created);

        auto asA = sendTestRequest(
            f.a.baseUrl, Get, "/v1/conversations/" + f.conversationId + "/messages", f.a.token);
        REQUIRE(asA.status == k200OK);
        REQUIRE(asA.json["messages"].size() == 3u);
        CHECK(asA.json["messages"][0]["content"].asString() == "first");
        CHECK(asA.json["messages"][1]["content"].asString() == "second");
        CHECK(asA.json["messages"][2]["type"].asString() == "voice");

        auto asB = sendTestRequest(
            f.b.baseUrl, Get, "/v1/conversations/" + f.conversationId + "/messages", f.b.token);
        REQUIRE(asB.status == k200OK);
        CHECK(asB.json["messages"].size() == 3u);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// CHECKS: a conversation with no messages yet returns an empty array, not
// an error.
DROGON_TEST(ListMessagesOnFreshConversationReturnsEmptyArray)
{
    try
    {
        auto f = setUpConversation();

        auto resp = sendTestRequest(
            f.a.baseUrl, Get, "/v1/conversations/" + f.conversationId + "/messages", f.a.token);
        REQUIRE(resp.status == k200OK);
        CHECK(resp.json["messages"].isArray());
        CHECK(resp.json["messages"].empty());
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// CHECKS: a non-participant gets 403, and reading history is still
// allowed on an expired conversation (only posting is blocked there —
// mirrors PostMessageToExpiredRejected409ButPinkyPromisedStillAccepts).
DROGON_TEST(ListMessagesRejectsNonParticipantButAllowsReadingExpiredHistory)
{
    try
    {
        auto f = setUpConversation();
        REQUIRE(postMessage(f.a, f.conversationId, "text", "before expiry").status ==
                k201Created);

        auto stranger = setUpTestSession();
        auto forbidden = sendTestRequest(stranger.baseUrl,
                                          Get,
                                          "/v1/conversations/" + f.conversationId + "/messages",
                                          stranger.token);
        CHECK(forbidden.status == k403Forbidden);
        CHECK(forbidden.json["error"].asString() == "NOT_A_PARTICIPANT");

        dbSetCreatedAt(f.conversationId, nowEpoch() - 4 * D);
        auto stillReadable = sendTestRequest(
            f.a.baseUrl, Get, "/v1/conversations/" + f.conversationId + "/messages", f.a.token);
        REQUIRE(stillReadable.status == k200OK);
        CHECK(stillReadable.json["messages"].size() == 1u);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

DROGON_TEST(ListMessagesUnknownConversationReturns404WithoutAuthReturns401)
{
    try
    {
        auto s = setUpTestSession();
        auto notFound = sendTestRequest(s.baseUrl,
                                         Get,
                                         "/v1/conversations/00000000-0000-0000-0000-000000000000/"
                                         "messages",
                                         s.token);
        CHECK(notFound.status == k404NotFound);

        auto f = setUpConversation();
        auto noAuth = sendTestRequest(f.a.baseUrl,
                                       Get,
                                       "/v1/conversations/" + f.conversationId + "/messages",
                                       /*bearerToken=*/"");
        CHECK(noAuth.status == k401Unauthorized);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

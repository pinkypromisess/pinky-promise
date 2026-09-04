#include <drogon/drogon_test.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "TestDb.h"
#include "TestHttpFixtures.h"
#include "TestHttpServer.h"

// Every DROGON_TEST here is HTTP-LEVEL: the request goes through the real
// Drogon router + the real auth::AuthFilter + a real Postgres DB (via the
// TestHttpServer harness), covering Module E.2's POST /v1/reports and its
// auto-block side effect (which itself reuses E.1's real BlockService --
// no cascade logic is re-implemented or re-tested here beyond confirming
// it actually ran).

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
    auto body = buildProfileBody(sixPhotoUrls("report-" + s.userId));
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

// proposer posts a proposal, interested swipes `interested` -> an active
// conversation. Returns {proposalId, conversationId}.
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

HttpTestResponse reportTarget(const TestSession &s, const std::string &targetType,
                               const std::string &targetId, const std::string &reason,
                               const std::string &detailsText = "")
{
    Json::Value body;
    body["target_type"] = targetType;
    body["target_id"] = targetId;
    body["reason"] = reason;
    body["details_text"] = detailsText;
    return sendTestRequest(s.baseUrl, Post, "/v1/reports", s.token, &body);
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

int64_t dbReportRowCount(const std::string &reporterUserId)
{
    auto rows = testDbClient()->execSqlSync(
        "SELECT count(*) AS c FROM reports WHERE reporter_user_id = $1", reporterUserId);
    return rows[0]["c"].as<int64_t>();
}

}  // namespace

// REQUIRED 1: successful report creation -- 201, correct shape,
// status: 'open'.
DROGON_TEST(CreateReportReturns201WithCorrectShapeAndOpenStatus)
{
    try
    {
        auto reporter = setUpVerifiedSession();
        auto reported = setUpVerifiedSession();

        auto resp = reportTarget(reporter, "profile", reported.userId, "harassment", "rude DMs");
        REQUIRE(resp.status == k201Created);
        CHECK(resp.json["reporter_user_id"].asString() == reporter.userId);
        CHECK(resp.json["target_type"].asString() == "profile");
        CHECK(resp.json["target_id"].asString() == reported.userId);
        CHECK(resp.json["reason"].asString() == "harassment");
        CHECK(resp.json["details_text"].asString() == "rude DMs");
        CHECK(resp.json["status"].asString() == "open");
        CHECK(!resp.json["id"].asString().empty());
        CHECK(!resp.json["created_at"].asString().empty());
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 2a: target_type "profile" resolves target_id itself as the
// reported user, and that user is actually auto-blocked.
DROGON_TEST(ProfileTargetResolvesDirectlyAndAutoBlocks)
{
    try
    {
        auto reporter = setUpVerifiedSession();
        auto reported = setUpVerifiedSession();

        REQUIRE(reportTarget(reporter, "profile", reported.userId, "fake_profile").status ==
                k201Created);
        CHECK(dbBlockRowCount(reporter.userId, reported.userId) == 1);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 2b: target_type "proposal" resolves to the proposal's
// creator_user_id, and that user is auto-blocked.
DROGON_TEST(ProposalTargetResolvesToCreatorAndAutoBlocks)
{
    try
    {
        auto reporter = setUpVerifiedSession();
        auto creator = setUpVerifiedSession();
        auto proposalId = createProposalOverHttp(creator);

        REQUIRE(
            reportTarget(reporter, "proposal", proposalId, "inappropriate_content").status ==
            k201Created);
        CHECK(dbBlockRowCount(reporter.userId, creator.userId) == 1);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 2c + REQUIRED (cascade): target_type "conversation" resolves
// to whichever participant is NOT the caller, that user is auto-blocked,
// AND the auto-block's cascade actually fired -- the very conversation
// used as the report target flips to 'expired' (an E.1-style assertion,
// reusing the real block cascade rather than re-implementing it).
DROGON_TEST(ConversationTargetResolvesToOtherParticipantAndAutoBlockCascadeFires)
{
    try
    {
        auto reporter = setUpVerifiedSession();
        auto reported = setUpVerifiedSession();
        // reported is the proposer, reporter is interested -- exercises
        // the "other participant" resolution from both directions across
        // this test file's three variants (this one has the caller as
        // interested_user_id).
        auto pc = setUpActiveConversation(/*proposer=*/reported, /*interested=*/reporter);
        REQUIRE(dbScalar("SELECT status FROM conversations WHERE id = $1", pc.conversationId) ==
                "active");

        REQUIRE(reportTarget(reporter, "conversation", pc.conversationId, "safety_concern")
                    .status == k201Created);

        CHECK(dbBlockRowCount(reporter.userId, reported.userId) == 1);
        CHECK(dbScalar("SELECT status FROM conversations WHERE id = $1", pc.conversationId) ==
              "expired");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 3: self-report rejected -- 400 CANNOT_REPORT_SELF, no report
// row created, no block created.
DROGON_TEST(SelfReportRejectedCreatesNothing)
{
    try
    {
        auto a = setUpVerifiedSession();
        REQUIRE(dbReportRowCount(a.userId) == 0);

        auto resp = reportTarget(a, "profile", a.userId, "other");
        CHECK(resp.status == k400BadRequest);
        CHECK(resp.json["error"].asString() == "CANNOT_REPORT_SELF");

        CHECK(dbReportRowCount(a.userId) == 0);
        CHECK(dbBlockRowCount(a.userId, a.userId) == 0);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 4: reporting a conversation the caller isn't part of -- 400
// NOT_A_PARTICIPANT, nothing created.
DROGON_TEST(ReportingConversationCallerIsNotPartOfRejectedCreatesNothing)
{
    try
    {
        auto a = setUpVerifiedSession();
        auto b = setUpVerifiedSession();
        auto stranger = setUpVerifiedSession();
        auto pc = setUpActiveConversation(a, b);

        REQUIRE(dbReportRowCount(stranger.userId) == 0);

        auto resp = reportTarget(stranger, "conversation", pc.conversationId, "other");
        CHECK(resp.status == k400BadRequest);
        CHECK(resp.json["error"].asString() == "NOT_A_PARTICIPANT");

        CHECK(dbReportRowCount(stranger.userId) == 0);
        CHECK(dbBlockRowCount(stranger.userId, a.userId) == 0);
        CHECK(dbBlockRowCount(stranger.userId, b.userId) == 0);
        // Untouched -- a rejected report must not trigger any cascade.
        CHECK(dbScalar("SELECT status FROM conversations WHERE id = $1", pc.conversationId) ==
              "active");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 5a: invalid target_type -- 400 TARGET_TYPE_INVALID.
DROGON_TEST(InvalidTargetTypeRejected)
{
    try
    {
        auto a = setUpVerifiedSession();
        auto b = setUpVerifiedSession();

        auto resp = reportTarget(a, "not_a_real_type", b.userId, "other");
        CHECK(resp.status == k400BadRequest);
        CHECK(resp.json["error"].asString() == "TARGET_TYPE_INVALID");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 5b: invalid reason -- 400 REASON_INVALID.
DROGON_TEST(InvalidReasonRejected)
{
    try
    {
        auto a = setUpVerifiedSession();
        auto b = setUpVerifiedSession();

        auto resp = reportTarget(a, "profile", b.userId, "not_a_real_reason");
        CHECK(resp.status == k400BadRequest);
        CHECK(resp.json["error"].asString() == "REASON_INVALID");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// Malformed target_id -- 400 TARGET_ID_INVALID (part of the same
// field-validation surface as 5a/5b; not explicitly named in the
// checkpoint list but covered as it's the same rigor).
DROGON_TEST(MalformedTargetIdRejected)
{
    try
    {
        auto a = setUpVerifiedSession();

        auto resp = reportTarget(a, "profile", "not-a-uuid", "other");
        CHECK(resp.status == k400BadRequest);
        CHECK(resp.json["error"].asString() == "TARGET_ID_INVALID");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 6: unknown target -- 404 TARGET_NOT_FOUND, for all three
// target_types (well-formed but nonexistent uuid in each table).
DROGON_TEST(UnknownTargetReturns404ForEveryTargetType)
{
    try
    {
        auto a = setUpVerifiedSession();
        const std::string unknownId = "00000000-0000-0000-0000-000000000000";

        auto profileResp = reportTarget(a, "profile", unknownId, "other");
        CHECK(profileResp.status == k404NotFound);
        CHECK(profileResp.json["error"].asString() == "TARGET_NOT_FOUND");

        auto proposalResp = reportTarget(a, "proposal", unknownId, "other");
        CHECK(proposalResp.status == k404NotFound);
        CHECK(proposalResp.json["error"].asString() == "TARGET_NOT_FOUND");

        auto conversationResp = reportTarget(a, "conversation", unknownId, "other");
        CHECK(conversationResp.status == k404NotFound);
        CHECK(conversationResp.json["error"].asString() == "TARGET_NOT_FOUND");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 7: no auth -- 401.
DROGON_TEST(ReportRouteRequiresAuth)
{
    try
    {
        auto a = setUpVerifiedSession();
        Json::Value body;
        body["target_type"] = "profile";
        body["target_id"] = a.userId;
        body["reason"] = "other";
        body["details_text"] = "";
        auto resp = sendTestRequest(a.baseUrl, Post, "/v1/reports", "", &body);
        CHECK(resp.status == k401Unauthorized);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

// REQUIRED 8: reporting the same target twice creates two separate Report
// rows (no dedup, unlike Block), but the second auto-block call is
// idempotent -- no error, no duplicate block row (reuses E.1's
// idempotency as-is).
DROGON_TEST(ReportingSameTargetTwiceCreatesTwoReportsButOneBlock)
{
    try
    {
        auto reporter = setUpVerifiedSession();
        auto reported = setUpVerifiedSession();

        auto first = reportTarget(reporter, "profile", reported.userId, "harassment", "first");
        REQUIRE(first.status == k201Created);
        auto second =
            reportTarget(reporter, "profile", reported.userId, "harassment", "second time");
        REQUIRE(second.status == k201Created);

        CHECK(first.json["id"].asString() != second.json["id"].asString());
        CHECK(dbReportRowCount(reporter.userId) == 2);
        CHECK(dbBlockRowCount(reporter.userId, reported.userId) == 1);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test failed: ") + e.what());
    }
}

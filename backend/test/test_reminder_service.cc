// D.1 has NO HTTP endpoint (CUJ #5 / entities doc: "no user-facing
// endpoint. A scheduled job queries PinkyPromise(status=confirmed) joined
// to Proposal.event_time and fires push notifications ~1hr out.") so the
// usual HTTP-level test bar does not apply the same way here -- there is
// no route to drive.
//
// Every DROGON_TEST in this file is instead a DB-INTEGRATION-LEVEL test:
// it drives real HTTP calls (POST /v1/proposals, POST /v1/proposals/{id}/
// swipe, POST /v1/conversations/{id}/pinky-promise, POST /v1/pinky-
// promises/{id}/confirm -- through the real Drogon router + auth::
// AuthFilter, via the TestHttpServer harness) to produce a real confirmed
// PinkyPromise backed by a real Postgres row, then calls
// app_context::reminderService()'s ensurePendingReminders() /
// fireDueReminders() DIRECTLY (there is no route to call them through) and
// asserts against the real `reminders` table and the shared
// StubReminderProvider. This is deliberately NOT an HTTP-level test of the
// sweep itself, since D.1 defines no endpoint for it.

#include <drogon/drogon_test.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/AppContext.h"
#include "../src/storage/TimeUtils.h"
#include "TestDb.h"
#include "TestHttpFixtures.h"
#include "TestHttpServer.h"

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
    auto body = buildProfileBody(sixPhotoUrls("rem-" + s.userId));
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

// `eventTimeIso` lets each test control the Proposal's event_time (and
// hence the derived reminders.scheduled_for = event_time - 1h) precisely,
// relative to wall-clock "now" at test run time.
std::string createProposalOverHttp(const TestSession &creator, const std::string &eventTimeIso)
{
    Json::Value body;
    body["activity_text"] = "grab coffee";
    body["event_time"] = eventTimeIso;
    Json::Value loc;
    loc["lat"] = 37.7749;
    loc["lng"] = -122.4194;
    loc["address"] = "123 Main St, San Francisco, CA";
    body["location"] = loc;
    body["payment_type"] = "split";
    body["looking_for_text"] = "someone chill to talk with";

    auto resp = sendTestRequest(creator.baseUrl, Post, "/v1/proposals", creator.token, &body);
    if (resp.status != k201Created)
    {
        throw std::runtime_error("POST /v1/proposals -> " + std::to_string(resp.status));
    }
    return resp.json["id"].asString();
}

HttpTestResponse swipeInterested(const TestSession &s, const std::string &proposalId)
{
    Json::Value body;
    body["action"] = "interested";
    return sendTestRequest(
        s.baseUrl, Post, "/v1/proposals/" + proposalId + "/swipe", s.token, &body);
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

int64_t epochNow()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

int64_t dbEpoch(const std::string &sql, const std::string &arg)
{
    auto rows = testDbClient()->execSqlSync(sql, arg);
    return rows[0][0].as<int64_t>();
}

std::string dbStatus(const std::string &sql, const std::string &arg)
{
    auto rows = testDbClient()->execSqlSync(sql, arg);
    return rows[0][0].as<std::string>();
}

struct Fixture
{
    TestSession a;  // proposer / initiator / user_a
    TestSession b;  // interested / confirmer / user_b
    std::string proposalId;
    std::string pinkyPromiseId;
};

// Drives the full real flow (proposal -> interested swipe -> conversation
// -> initiate -> confirm) over HTTP to produce a genuinely confirmed
// PinkyPromise -- never fabricates rows that could not actually exist --
// with the Proposal's event_time set to `now + eventOffset`.
Fixture setUpConfirmedPinkyPromise(std::chrono::system_clock::duration eventOffset)
{
    Fixture f;
    f.a = setUpVerifiedSession();
    f.b = setUpVerifiedSession();
    const auto eventTimeIso = storage::formatIso8601Utc(std::chrono::system_clock::now() + eventOffset);
    f.proposalId = createProposalOverHttp(f.a, eventTimeIso);

    if (swipeInterested(f.b, f.proposalId).status != k201Created)
    {
        throw std::runtime_error("interested swipe failed");
    }
    const auto conversationId = conversationIdFor(f.b, f.proposalId);

    auto init = initiatePp(f.a, conversationId);
    if (init.status != k201Created)
    {
        throw std::runtime_error("initiate pinky-promise -> " + std::to_string(init.status));
    }
    f.pinkyPromiseId = init.json["id"].asString();

    auto conf = confirmPp(f.b, f.pinkyPromiseId);
    if (conf.status != k200OK)
    {
        throw std::runtime_error("confirm pinky-promise -> " + std::to_string(conf.status));
    }
    return f;
}

}  // namespace

// REQUIRED: ensurePendingReminders() creates a row with the correct
// scheduled_for (event_time - 1h) for a real confirmed PinkyPromise, and is
// idempotent on a second call (no duplicate row, no error).
DROGON_TEST(EnsurePendingRemindersCreatesRowAndIsIdempotent)
{
    try
    {
        // event_time 3h out -> comfortably future on both ends, nothing due.
        auto f = setUpConfirmedPinkyPromise(std::chrono::hours(3));

        // ensurePendingReminders() is a genuine global sweep over
        // `pinky_promises`, and this suite runs against a persistent,
        // shared test DB (other tests/files leave their own confirmed
        // future-event PinkyPromises behind) -- so its return value can be
        // larger than 1. What this test actually owns and can assert
        // precisely is whether OUR fixture's PinkyPromise got exactly one
        // correctly-computed row, which the checks below verify directly
        // against f.pinkyPromiseId rather than trusting the global count.
        const auto created = app_context::reminderService().ensurePendingReminders();
        CHECK(created >= 1);

        auto countRows = testDbClient()->execSqlSync(
            "SELECT count(*) FROM reminders WHERE pinky_promise_id = $1", f.pinkyPromiseId);
        CHECK(countRows[0][0].as<int64_t>() == 1);

        // scheduled_for = event_time - 1h is a pure SQL interval
        // subtraction (not derived from now()), so exact equality is safe
        // -- no tolerance needed here, unlike the now()-derived checks
        // below.
        const auto eventEpoch =
            dbEpoch("SELECT FLOOR(EXTRACT(EPOCH FROM event_time))::bigint FROM proposals WHERE id = $1",
                    f.proposalId);
        const auto scheduledEpoch = dbEpoch(
            "SELECT FLOOR(EXTRACT(EPOCH FROM scheduled_for))::bigint FROM reminders "
            "WHERE pinky_promise_id = $1",
            f.pinkyPromiseId);
        CHECK(scheduledEpoch == eventEpoch - 3600);
        CHECK(dbStatus("SELECT status FROM reminders WHERE pinky_promise_id = $1",
                        f.pinkyPromiseId) == "pending");

        // Idempotent: second call creates nothing more, no exception, still one row.
        const auto createdAgain = app_context::reminderService().ensurePendingReminders();
        CHECK(createdAgain == 0);
        auto countRows2 = testDbClient()->execSqlSync(
            "SELECT count(*) FROM reminders WHERE pinky_promise_id = $1", f.pinkyPromiseId);
        CHECK(countRows2[0][0].as<int64_t>() == 1);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("test failed: ") + e.what());
    }
}

// REQUIRED: fireDueReminders() fires (provider called for both users, row
// -> sent) when scheduled_for <= now() and the PP/proposal are still
// confirmed/pinky_promised.
DROGON_TEST(FireDueRemindersFiresWhenDueAndStillConfirmed)
{
    try
    {
        // event_time 30 minutes out -> still future (ensurePendingReminders
        // will create the row) but scheduled_for = event_time - 1h is 30
        // minutes in the PAST -> already due.
        auto f = setUpConfirmedPinkyPromise(std::chrono::minutes(30));
        REQUIRE(app_context::reminderService().ensurePendingReminders() == 1);

        auto provider = testServerReminderProvider();
        provider->reset();

        const auto result = app_context::reminderService().fireDueReminders();
        CHECK(result.fired == 1);
        CHECK(result.skipped == 0);

        CHECK(dbStatus("SELECT status FROM reminders WHERE pinky_promise_id = $1",
                        f.pinkyPromiseId) == "sent");

        const auto sentAtEpoch = dbEpoch(
            "SELECT FLOOR(EXTRACT(EPOCH FROM sent_at))::bigint FROM reminders "
            "WHERE pinky_promise_id = $1",
            f.pinkyPromiseId);
        // sent_at is set via the DB's now() -- allow a small tolerance
        // against CI scheduler jitter rather than exact equality.
        CHECK(std::llabs(sentAtEpoch - epochNow()) <= 5);

        auto sent = provider->sent();
        REQUIRE(sent.size() == 2);
        bool sawA = false, sawB = false;
        for (const auto &s : sent)
        {
            CHECK(s.pinkyPromiseId == f.pinkyPromiseId);
            if (s.userId == f.a.userId) sawA = true;
            if (s.userId == f.b.userId) sawB = true;
        }
        CHECK(sawA);
        CHECK(sawB);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("test failed: ") + e.what());
    }
}

// REQUIRED: fireDueReminders() does NOT fire a row whose scheduled_for is
// still in the future (stays pending, provider not called).
DROGON_TEST(FireDueRemindersLeavesFutureRowPending)
{
    try
    {
        // event_time 3h out -> scheduled_for = now + 2h, comfortably future.
        auto f = setUpConfirmedPinkyPromise(std::chrono::hours(3));
        REQUIRE(app_context::reminderService().ensurePendingReminders() == 1);

        auto provider = testServerReminderProvider();
        provider->reset();

        const auto result = app_context::reminderService().fireDueReminders();
        CHECK(result.fired == 0);
        CHECK(result.skipped == 0);

        CHECK(dbStatus("SELECT status FROM reminders WHERE pinky_promise_id = $1",
                        f.pinkyPromiseId) == "pending");

        for (const auto &s : provider->sent())
        {
            CHECK(s.pinkyPromiseId != f.pinkyPromiseId);
        }
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("test failed: ") + e.what());
    }
}

// REQUIRED: fireDueReminders() marks a row `skipped` (not `sent`, provider
// not called) when the PinkyPromise is no longer `confirmed` at fire time
// -- proves the re-verification-at-send-time design works without any
// cross-file wiring (the PinkyPromise is cancelled here by directly
// updating the DB, exactly the way a delete-Proposal or Block cascade in
// another module would, but D.1 needs no wiring to that cascade for this
// to work).
DROGON_TEST(FireDueRemindersSkipsWhenNoLongerConfirmed)
{
    try
    {
        auto f = setUpConfirmedPinkyPromise(std::chrono::minutes(30));  // already due once created
        REQUIRE(app_context::reminderService().ensurePendingReminders() == 1);

        // Simulate what a cascade in another module (delete-Proposal,
        // Block) would eventually do -- cancel the PinkyPromise -- without
        // touching any other module's files.
        testDbClient()->execSqlSync(
            "UPDATE pinky_promises SET status = 'cancelled' WHERE id = $1", f.pinkyPromiseId);

        auto provider = testServerReminderProvider();
        provider->reset();

        const auto result = app_context::reminderService().fireDueReminders();
        CHECK(result.fired == 0);
        CHECK(result.skipped == 1);

        CHECK(dbStatus("SELECT status FROM reminders WHERE pinky_promise_id = $1",
                        f.pinkyPromiseId) == "skipped");
        CHECK(provider->sent().empty());
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("test failed: ") + e.what());
    }
}

// REQUIRED: calling fireDueReminders() twice on an already-sent row does
// not re-call the provider (no double-send).
DROGON_TEST(FireDueRemindersDoesNotDoubleSend)
{
    try
    {
        auto f = setUpConfirmedPinkyPromise(std::chrono::minutes(30));
        REQUIRE(app_context::reminderService().ensurePendingReminders() == 1);

        auto provider = testServerReminderProvider();
        provider->reset();

        const auto first = app_context::reminderService().fireDueReminders();
        CHECK(first.fired == 1);
        REQUIRE(provider->sent().size() == 2);

        const auto second = app_context::reminderService().fireDueReminders();
        CHECK(second.fired == 0);
        CHECK(second.skipped == 0);
        // No new calls to the provider -- still exactly the 2 from the first call.
        CHECK(provider->sent().size() == 2);

        CHECK(dbStatus("SELECT status FROM reminders WHERE pinky_promise_id = $1",
                        f.pinkyPromiseId) == "sent");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("test failed: ") + e.what());
    }
}

// CHECKS: runSweep() composes both steps end-to-end (create then fire) in
// one call, matching what a later micro-step's periodic trigger will call.
DROGON_TEST(RunSweepCreatesThenFires)
{
    try
    {
        auto f = setUpConfirmedPinkyPromise(std::chrono::minutes(30));
        auto provider = testServerReminderProvider();
        provider->reset();

        const auto result = app_context::reminderService().runSweep();
        CHECK(result.remindersCreated == 1);
        CHECK(result.remindersFired == 1);
        CHECK(result.remindersSkipped == 0);
        CHECK(provider->sent().size() == 2);
        CHECK(dbStatus("SELECT status FROM reminders WHERE pinky_promise_id = $1",
                        f.pinkyPromiseId) == "sent");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("test failed: ") + e.what());
    }
}

// D.2 adds NO HTTP endpoint either -- it wires a periodic background timer
// to a function D.1 already exercises directly (see the file comment in
// test_reminder_service.cc). This file's single DROGON_TEST is therefore
// also DB-INTEGRATION-LEVEL, not HTTP-level: it drives real HTTP calls to
// produce a genuinely confirmed PinkyPromise, exactly like
// test_reminder_service.cc, but this time never calls runSweep() directly
// -- instead it constructs a real services::ReminderSweepScheduler with an
// artificially short interval and asserts the reminder actually gets
// fired by the real timer/dedicated-thread mechanism, via bounded polling
// (never a fixed sleep) so a hang can't stall the rest of the suite.
//
// This is deliberately the ONLY thing tested here. The sweep's actual
// decision logic (create vs. skip vs. fire, re-verification at send time,
// no-double-send) is already fully covered by test_reminder_service.cc's
// direct runSweep()/fireDueReminders() calls -- re-deriving all of that
// through a real wall-clock timer would just make those same assertions
// slower and flakier for no added coverage. What IS new surface in D.2,
// and therefore what this test targets, is the scheduler itself:
// constructing it, starting its dedicated thread, and confirming a real
// periodic callback on that thread actually reaches ReminderService and
// mutates the DB -- plus (via this test's own scope-exit) that tearing it
// down doesn't hang or crash the process.

#include <drogon/drogon_test.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../src/services/ReminderSweepScheduler.h"
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
    auto body = buildProfileBody(sixPhotoUrls("sched-" + s.userId));
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

struct Fixture
{
    TestSession a;
    TestSession b;
    std::string proposalId;
    std::string pinkyPromiseId;
};

// event_time 30 minutes out -> scheduled_for (event_time - 1h) is already
// 30 minutes in the past, so a single sweep tick both creates AND fires
// this reminder (mirrors test_reminder_service.cc's RunSweepCreatesThen-
// Fires fixture, just reused here to trigger via the real timer instead).
Fixture setUpConfirmedPinkyPromiseDueSoon()
{
    Fixture f;
    f.a = setUpVerifiedSession();
    f.b = setUpVerifiedSession();
    const auto eventTimeIso =
        storage::formatIso8601Utc(std::chrono::system_clock::now() + std::chrono::minutes(30));
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

std::string dbStatus(const std::string &sql, const std::string &arg)
{
    auto rows = testDbClient()->execSqlSync(sql, arg);
    return rows[0][0].as<std::string>();
}

// Bounded poll -- never a single fixed sleep -- so a mechanism that never
// fires fails this test promptly instead of hanging the suite.
bool waitUntil(const std::function<bool()> &predicate, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return predicate();
}

}  // namespace

// Exercises the actual periodic mechanism end-to-end: construct a real
// ReminderSweepScheduler with a short interval, let its dedicated thread's
// timer fire on its own (never calling runSweep() directly), and confirm
// the reminder this test seeded really does transition pending -> sent
// via the provider and the DB, within a bounded wait. The scheduler goes
// out of scope (and its thread is torn down) at the end of this test with
// no crash/hang, which is this test's other assertion -- implicitly, by
// the test completing at all.
DROGON_TEST(SchedulerFiresDueReminderOnItsOwnTimer)
{
    try
    {
        auto f = setUpConfirmedPinkyPromiseDueSoon();

        auto provider = testServerReminderProvider();
        provider->reset();

        // 1s interval: comfortably short for a bounded test wait, nowhere
        // near production's kDefaultInterval (60s, used unconditionally by
        // main.cc).
        services::ReminderSweepScheduler scheduler(std::chrono::seconds(1));

        const bool fired = waitUntil(
            [&]() { return provider->sent().size() >= 2; }, std::chrono::seconds(10));
        REQUIRE(fired);

        CHECK(dbStatus("SELECT status FROM reminders WHERE pinky_promise_id = $1",
                        f.pinkyPromiseId) == "sent");

        bool sawA = false, sawB = false;
        for (const auto &s : provider->sent())
        {
            if (s.pinkyPromiseId != f.pinkyPromiseId)
            {
                continue;
            }
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

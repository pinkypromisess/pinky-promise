#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include "ReminderProvider.h"

namespace notifications
{
struct SentReminder
{
    std::string userId;
    std::string pinkyPromiseId;
    std::string activityText;
    std::chrono::system_clock::time_point eventTime;
};

// Deterministic fake: records what it was asked to send into an in-memory
// vector instead of delivering anything real. Used for local development
// and tests so the reminder sweep runs end-to-end without real push
// infrastructure -- mirrors verification::StubFaceVerificationProvider /
// storage::StubGcsUploadUrlProvider.
//
// When this instance is shared with a running Drogon app under test (see
// backend/test/TestHttpServer.h), ReminderService::fireDueReminders() may
// be invoked directly from a test's own thread (there is no HTTP endpoint
// for the sweep -- see backend/src/services/ReminderService.h), but the
// same mutex-guarded pattern as StubFaceVerificationProvider is kept here
// regardless, since nothing prevents a future caller from driving the
// sweep off the event-loop thread instead.
class StubReminderProvider : public ReminderProvider
{
  public:
    void sendReminder(const std::string &userId,
                       const std::string &pinkyPromiseId,
                       const std::string &activityText,
                       std::chrono::system_clock::time_point eventTime) override;

    // Test hook: everything recorded so far, in call order.
    std::vector<SentReminder> sent() const;

    // Test hook: clears recorded sends. Tests share one instance across
    // DROGON_TESTs via the TestHttpServer singleton, so each test that
    // asserts on sent() should reset() first.
    void reset();

  private:
    mutable std::mutex mutex_;
    std::vector<SentReminder> sent_;
};

}  // namespace notifications

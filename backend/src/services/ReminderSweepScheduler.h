#pragma once

#include <trantor/net/EventLoopThread.h>

#include <chrono>

namespace services
{
// Periodically calls app_context::reminderService().runSweep() (D.1's
// ensurePendingReminders() + fireDueReminders()) on a dedicated background
// loop -- deliberately NOT Drogon's main IO loop. runSweep() does blocking
// execSqlSync DB calls (see ReminderService.cc), and firing it directly
// off a timer on the main loop would stall request handling for however
// long the sweep takes. Same pattern as
// storage::GcsSignBlobUploadUrlProvider's ioThread_ (see that class's
// header for the fuller rationale) -- a dedicated trantor::EventLoopThread
// whose loop the periodic timer is scheduled on instead.
//
// Lifetime: this object's destructor (via ~EventLoopThread) stops the
// dedicated thread and its timer, so an instance must be kept alive for as
// long as periodic sweeps should keep firing -- see main.cc, which holds
// one in a function-local static shared_ptr for the process's lifetime. A
// local that went out of scope right after construction would tear the
// thread back down immediately, silently turning the "periodic" trigger
// into a no-op.
//
// The timer callback itself (see the .cc) captures NOTHING: it reaches
// ReminderService purely through the free function
// app_context::reminderService(), which is itself kept alive in
// AppContext.cc's own static storage for the process's lifetime. So
// there's no `this`/local-reference lifetime hazard for the callback to
// reason about at all -- it never captures this scheduler object, `db_`,
// or any provider by reference, satisfying CLAUDE.md's flat rule on
// async/callback captures by construction rather than by argument.
class ReminderSweepScheduler
{
  public:
    // Interval between sweep runs in production (see main.cc). A cheap
    // query against 1-hour-out reminders has nothing time-critical to the
    // second, so a full minute of slop is fine.
    static constexpr std::chrono::seconds kDefaultInterval{60};

    // Starts the dedicated loop thread and schedules runSweep() to run
    // every `interval` on it (first fire at now + interval, per trantor's
    // EventLoop::runEvery semantics, then every `interval` thereafter).
    // `interval` is a constructor parameter rather than baked in purely so
    // a test can pass an artificially short one -- production code should
    // just use the kDefaultInterval default.
    explicit ReminderSweepScheduler(std::chrono::seconds interval = kDefaultInterval);

  private:
    // Implicitly makes this class non-copyable/non-movable too
    // (trantor::EventLoopThread is itself NonCopyable) -- there is no
    // scenario in this app where either is needed.
    trantor::EventLoopThread ioThread_;
};

}  // namespace services

#include "ReminderSweepScheduler.h"

#include "../AppContext.h"

namespace services
{
ReminderSweepScheduler::ReminderSweepScheduler(std::chrono::seconds interval)
  : ioThread_("reminder-sweep")
{
    ioThread_.run();

    // Capture-less lambda: the ONLY thing this callback touches is the
    // free function app_context::reminderService(), never a captured
    // `this`, `db_`, or provider reference -- see the header comment for
    // why that sidesteps the whole "does the referenced thing outlive the
    // callback" question rather than just answering it carefully. Runs
    // repeatedly on ioThread_'s loop (not the main app loop) for as long
    // as this scheduler object -- and therefore ioThread_ -- stays alive.
    ioThread_.getLoop()->runEvery(interval, []() { app_context::reminderService().runSweep(); });
}

}  // namespace services

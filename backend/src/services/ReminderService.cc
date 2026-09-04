#include "ReminderService.h"

#include <cstdint>

namespace services
{
namespace
{
std::chrono::system_clock::time_point epochToTp(int64_t epochSeconds)
{
    return std::chrono::system_clock::time_point(std::chrono::seconds(epochSeconds));
}

}  // namespace

ReminderService::ReminderService(drogon::orm::DbClientPtr db,
                                  std::shared_ptr<notifications::ReminderProvider> provider)
  : db_(std::move(db)), provider_(std::move(provider))
{
}

int ReminderService::ensurePendingReminders()
{
    // Set-based insert: every confirmed PinkyPromise with a future event
    // and no existing `reminders` row gets one. ON CONFLICT DO NOTHING
    // (against the UNIQUE(pinky_promise_id) constraint from migration 010)
    // is the idempotency guarantee -- a second call finds nothing left to
    // insert (the NOT EXISTS already excludes it) and, even under a
    // concurrent racing call, ON CONFLICT swallows the duplicate at the
    // statement level rather than throwing. No bind parameters are needed
    // since nothing here comes from a request.
    auto rows = db_->execSqlSync(
        "INSERT INTO reminders (pinky_promise_id, scheduled_for) "
        "SELECT pp.id, p.event_time - interval '1 hour' "
        "FROM pinky_promises pp "
        "JOIN proposals p ON p.id = pp.proposal_id "
        "WHERE pp.status = 'confirmed' "
        "  AND p.event_time > now() "
        "  AND NOT EXISTS (SELECT 1 FROM reminders r WHERE r.pinky_promise_id = pp.id) "
        "ON CONFLICT (pinky_promise_id) DO NOTHING "
        "RETURNING id");
    return static_cast<int>(rows.size());
}

FireDueRemindersResult ReminderService::fireDueReminders()
{
    FireDueRemindersResult result;

    // One JOIN read of every due-and-still-pending reminder, re-verifying
    // the linked PinkyPromise/Proposal state as of THIS call -- not
    // whatever it was when the row was created by ensurePendingReminders().
    auto rows = db_->execSqlSync(
        "SELECT r.id AS reminder_id, pp.id AS pinky_promise_id, "
        "       pp.user_a_id, pp.user_b_id, pp.status AS pp_status, "
        "       p.status AS proposal_status, p.activity_text, "
        "       FLOOR(EXTRACT(EPOCH FROM p.event_time))::bigint AS event_time_epoch "
        "FROM reminders r "
        "JOIN pinky_promises pp ON pp.id = r.pinky_promise_id "
        "JOIN proposals p ON p.id = pp.proposal_id "
        "WHERE r.status = 'pending' AND r.scheduled_for <= now()");

    for (const auto &row : rows)
    {
        const auto reminderId = row["reminder_id"].as<std::string>();
        const bool stillConfirmed = row["pp_status"].as<std::string>() == "confirmed" &&
                                     row["proposal_status"].as<std::string>() == "pinky_promised";

        if (stillConfirmed)
        {
            const auto pinkyPromiseId = row["pinky_promise_id"].as<std::string>();
            const auto activityText = row["activity_text"].as<std::string>();
            const auto eventTime = epochToTp(row["event_time_epoch"].as<int64_t>());

            provider_->sendReminder(
                row["user_a_id"].as<std::string>(), pinkyPromiseId, activityText, eventTime);
            provider_->sendReminder(
                row["user_b_id"].as<std::string>(), pinkyPromiseId, activityText, eventTime);

            // `AND status = 'pending'` makes this a no-op (not a re-fire)
            // if the row was somehow already resolved between the SELECT
            // above and here -- the guard that also makes a second
            // fireDueReminders() call never re-call the provider for an
            // already-'sent' row (the SELECT's WHERE r.status = 'pending'
            // already excludes it, but this keeps the UPDATE itself
            // self-consistent rather than trusting the earlier read).
            auto updated = db_->execSqlSync(
                "UPDATE reminders SET status = 'sent', sent_at = now() "
                "WHERE id = $1 AND status = 'pending'",
                reminderId);
            if (updated.affectedRows() > 0)
            {
                ++result.fired;
            }
        }
        else
        {
            auto updated = db_->execSqlSync(
                "UPDATE reminders SET status = 'skipped' "
                "WHERE id = $1 AND status = 'pending'",
                reminderId);
            if (updated.affectedRows() > 0)
            {
                ++result.skipped;
            }
        }
    }

    return result;
}

ReminderSweepResult ReminderService::runSweep()
{
    ReminderSweepResult result;
    result.remindersCreated = ensurePendingReminders();
    const auto fireResult = fireDueReminders();
    result.remindersFired = fireResult.fired;
    result.remindersSkipped = fireResult.skipped;
    return result;
}

}  // namespace services

#pragma once

#include <drogon/orm/DbClient.h>

#include <memory>

#include "../notifications/ReminderProvider.h"

// Owns the CUJ #5 / entities-doc "Reminder" scheduled-job logic: "1 hour
// before a Pinky-Promised event's scheduled time, both users receive a
// reminder notification." Per the entities doc, this is explicitly a
// scheduled-job concern with NO user-facing HTTP endpoint -- there is no
// ReminderController, and runSweep() is meant to be called by a periodic
// trigger a later micro-step wires up (not built here).
//
// Because there's no HTTP layer, this is not shaped like the other
// services: no typed exceptions map to HTTP status codes (there's no
// request to reject), and the entry points return small result structs
// with counts instead.
namespace services
{
// Aggregate counts from one full runSweep() call.
struct ReminderSweepResult
{
    int remindersCreated = 0;
    int remindersFired = 0;
    int remindersSkipped = 0;
};

// Per-call result of fireDueReminders() alone (also exposed so a caller /
// test can distinguish "fired" from "skipped" without re-deriving it from
// ReminderSweepResult).
struct FireDueRemindersResult
{
    int fired = 0;
    int skipped = 0;
};

class ReminderService
{
  public:
    ReminderService(drogon::orm::DbClientPtr db,
                     std::shared_ptr<notifications::ReminderProvider> provider);

    // For every `pinky_promises` row with status = 'confirmed' whose
    // proposal's event_time is still in the future and that does not yet
    // have a `reminders` row, inserts one with
    // scheduled_for = event_time - interval '1 hour' and status =
    // 'pending'. Safe to call repeatedly: a set-based INSERT ... SELECT ...
    // ON CONFLICT (pinky_promise_id) DO NOTHING makes this naturally
    // idempotent in one statement, so a second call for the same
    // PinkyPromise is a silent no-op rather than a thrown unique-violation
    // (the UNIQUE(pinky_promise_id) constraint is still the backstop that
    // makes this safe under a concurrent caller). Returns the number of
    // rows actually inserted.
    int ensurePendingReminders();

    // For every `reminders` row with status = 'pending' AND
    // scheduled_for <= now(): re-verifies (via the same read) that the
    // linked PinkyPromise is still 'confirmed' AND its Proposal is still
    // 'pinky_promised'. If both hold, calls the ReminderProvider for both
    // user_a and user_b, then marks the row 'sent' (sent_at = now()). If
    // either check fails -- the PinkyPromise/Proposal moved on (cancelled,
    // etc.) since the reminder was scheduled -- marks the row 'skipped'
    // instead, without calling the provider. This re-check is the whole
    // point: no other module needs to proactively cancel a `reminders` row
    // when a PinkyPromise is cancelled, because this function never trusts
    // that a 'pending' row still represents a live commitment.
    //
    // Each row's status-check-and-update is its own single-row
    // parameterized UPDATE guarded by `AND status = 'pending'`, so calling
    // this twice (e.g. two sweep ticks) never re-fires or re-skips an
    // already-resolved row and never double-calls the provider.
    FireDueRemindersResult fireDueReminders();

    // ensurePendingReminders() followed by fireDueReminders() -- the full
    // sweep. This is what a later micro-step wires to a periodic trigger;
    // nothing in D.1 calls this off a timer.
    ReminderSweepResult runSweep();

  private:
    drogon::orm::DbClientPtr db_;
    std::shared_ptr<notifications::ReminderProvider> provider_;
};

}  // namespace services

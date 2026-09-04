-- 010_create_reminders.sql
-- Reminder (CUJ #5, entities doc's "no user-facing endpoint" scheduled-job
-- design): "1 hour before a Pinky-Promised event's scheduled time, both
-- users receive a reminder notification."
--
-- One row is created per confirmed PinkyPromise ahead of its event (see
-- ReminderService::ensurePendingReminders, src/services/ReminderService.cc)
-- with scheduled_for = proposals.event_time - interval '1 hour'. When
-- scheduled_for arrives, the sweep (ReminderService::fireDueReminders)
-- RE-VERIFIES pinky_promises.status = 'confirmed' AND proposals.status =
-- 'pinky_promised' at send time, rather than relying on any other module
-- (proposal delete, block, etc.) to proactively cancel this row. A
-- PinkyPromise that is no longer confirmed by fire time simply gets its
-- reminder marked 'skipped' instead of 'sent' -- no cross-module wiring
-- needed, no stale-reminder bug possible.

CREATE TABLE reminders (
    id                UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    -- One reminder row per PinkyPromise, ever -- UNIQUE is also the
    -- idempotency backstop for ensurePendingReminders() being safe to call
    -- repeatedly.
    pinky_promise_id  UUID NOT NULL UNIQUE REFERENCES pinky_promises(id) ON DELETE CASCADE,
    scheduled_for     TIMESTAMPTZ NOT NULL,
    status            TEXT NOT NULL DEFAULT 'pending'
                          CHECK (status IN ('pending', 'sent', 'skipped')),
    -- Set only when status transitions to 'sent'.
    sent_at           TIMESTAMPTZ,
    created_at        TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- fireDueReminders() scans for status = 'pending' AND scheduled_for <= now().
CREATE INDEX reminders_due_idx ON reminders (status, scheduled_for);

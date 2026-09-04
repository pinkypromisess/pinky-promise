-- 009_create_blocks_reports.sql
-- Module E. Creates BOTH `blocks` and `reports` together (assigned as one
-- migration file so a later micro-step doesn't need a second migration),
-- even though E.1 (this step) only reads/writes `blocks` -- `reports`
-- exists here unused, no endpoint or service logic yet, until E.2.

-- Block (CUJ #10, entities doc's Block entity): stored as one directional
-- row (blocker -> blocked) but treated symmetrically everywhere it's
-- consulted -- feed exclusion and "can a Conversation start" checks
-- (Module B/C follow-ups) look for a block in EITHER direction between a
-- pair, as does this module's own block-cascade (see BlockService.cc).
CREATE TABLE blocks (
    id                UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    blocker_user_id   UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    blocked_user_id   UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at        TIMESTAMPTZ NOT NULL DEFAULT now(),
    -- One block row per ORDERED pair -- re-blocking the same direction is
    -- idempotent at the service layer (POST /v1/blocks returns 200 with
    -- the existing row rather than erroring or inserting a duplicate).
    UNIQUE (blocker_user_id, blocked_user_id)
);

-- The UNIQUE constraint above already gives an efficient index for the
-- forward direction (blocker_user_id, blocked_user_id). This index covers
-- the reverse direction, so "is there a block between A and B in either
-- direction" (WHERE (blocker=A AND blocked=B) OR (blocker=B AND
-- blocked=A)) can use an index scan on each OR branch instead of a scan
-- over one column alone. Used by this migration's own block-cascade
-- (BlockService) and, per the entities doc, by future feed-exclusion /
-- conversation-start checks.
CREATE INDEX blocks_blocked_user_id_idx ON blocks (blocked_user_id, blocker_user_id);

-- Report (CUJ #10, entities doc's Report entity). Table only for E.1 --
-- no POST /v1/reports endpoint or ReportService yet (that's E.2). Reports
-- accumulate for manual periodic review (status stays 'open' until
-- reviewed by hand); no automated suspension logic for MVP.
CREATE TABLE reports (
    id                UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    reporter_user_id  UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    target_type       TEXT NOT NULL CHECK (target_type IN ('profile', 'proposal', 'conversation')),
    -- No FK: target_id points at whichever table target_type names, so a
    -- single column can't carry one REFERENCES clause.
    target_id         UUID NOT NULL,
    reason            TEXT NOT NULL
                          CHECK (reason IN ('harassment', 'fake_profile', 'inappropriate_content',
                                            'safety_concern', 'other')),
    details_text      TEXT NOT NULL,
    status            TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'reviewed')),
    created_at        TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Supports a future periodic-review query over open reports (E.2's
-- concern; indexed now since the table is being created now).
CREATE INDEX reports_status_created_at_idx ON reports (status, created_at DESC);

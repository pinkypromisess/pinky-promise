-- 006_create_swipes.sql
-- Swipe: what CUJ #3's Heart (interested) / X (pass) writes to.
-- See docs/pinky-promise-entities-api.md's Swipe entity.
--
-- One swipe per user per proposal, ever -- enforced by the
-- UNIQUE (proposal_id, swiper_user_id) constraint below. A second swipe
-- (even flipping interested<->pass) is rejected at the API layer with
-- 409, never silently upserted.
--
-- `pass` requires nothing. `interested` requires the swiper's
-- profiles.verified = true AND counts against the "10 interested / rolling
-- 24h" cap (CUJ #3 / Global rules). Both of those checks live in
-- src/services/SwipeService.cc; this table just stores the rows they
-- count.

CREATE TABLE swipes (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    proposal_id     UUID NOT NULL REFERENCES proposals(id) ON DELETE CASCADE,
    swiper_user_id  UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    action          TEXT NOT NULL CHECK (action IN ('interested', 'pass')),
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),

    CONSTRAINT swipes_one_per_user_per_proposal UNIQUE (proposal_id, swiper_user_id)
);

-- Supports the per-user rolling-24h "interested" count
-- (WHERE swiper_user_id = $1 AND action = 'interested'
--        AND created_at > now() - interval '24 hours').
-- Partial on action = 'interested' so it only indexes the rows that count
-- toward the cap.
CREATE INDEX swipes_interested_daily_idx
    ON swipes (swiper_user_id, created_at)
    WHERE action = 'interested';

-- Plain index on the swiper FK so a user deletion's ON DELETE CASCADE
-- doesn't seq-scan (the UNIQUE constraint already provides a
-- proposal_id-leading index, covering the proposals FK and the feed's
-- future "already swiped by this caller" anti-join).
CREATE INDEX swipes_swiper_user_id_idx ON swipes (swiper_user_id);

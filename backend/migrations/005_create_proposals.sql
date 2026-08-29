-- 005_create_proposals.sql
-- Proposal: one creator, proposal-specific fields per docs/pinky-promise-cujs.md
-- CUJ #1 and docs/pinky-promise-entities-api.md's Proposal entity.
--
-- revealed_fields[] (subset of {occupation, relationship_status}) is
-- modeled as two booleans rather than a text[] column -- there are only
-- ever two possible values, so this avoids Postgres array literal
-- encoding/parsing entirely while still round-tripping to the same
-- `revealed_fields: ["occupation", ...]` JSON array shape at the API
-- layer (see src/services/ProposalService.cc).
--
-- Note: swipes and blocks tables (owned by Module C / Module E) do not
-- exist yet in this repo. GET /proposals/feed's query therefore can't yet
-- exclude already-swiped or blocked-creator proposals -- see the TODOs in
-- ProposalService::getFeed.

CREATE TABLE proposals (
    id                          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    creator_user_id             UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    activity_text               TEXT NOT NULL,
    event_time                  TIMESTAMPTZ NOT NULL,
    location_lat                DOUBLE PRECISION NOT NULL,
    location_lng                DOUBLE PRECISION NOT NULL,
    location_address            TEXT NOT NULL,
    payment_type                TEXT NOT NULL
                                     CHECK (payment_type IN ('split', 'host_treats', 'guest_treats', 'tbd')),
    looking_for_text            TEXT NOT NULL,
    reveal_occupation           BOOLEAN NOT NULL DEFAULT false,
    reveal_relationship_status  BOOLEAN NOT NULL DEFAULT false,
    status                       TEXT NOT NULL DEFAULT 'active'
                                     CHECK (status IN ('active', 'pinky_promised', 'cancelled', 'expired')),
    created_at                  TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- The feed query filters on status + excludes the caller's own proposals,
-- then orders by created_at (recency) as the MVP ranking's default/tiebreaker.
CREATE INDEX proposals_feed_idx ON proposals (status, creator_user_id, created_at DESC);

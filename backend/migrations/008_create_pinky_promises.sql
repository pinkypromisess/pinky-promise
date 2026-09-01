-- 008_create_pinky_promises.sql
-- PinkyPromise (CUJ #4, entities doc's PinkyPromise + Section 5). Created
-- in `pending_b_confirm` when A taps the button on a Conversation; flips
-- to `confirmed` when B confirms. Only at `confirmed` does the winning
-- Conversation stop expiring, its Proposal move to `pinky_promised`, and
-- the sibling Conversations on that Proposal close.
--
-- The "at most 3 active Pinky-Promised events" rule (Global rules /
-- Section 5) is checked at confirm time against BOTH parties as
-- COUNT(status = 'confirmed' AND proposals.event_time > now()); it is not
-- stored.

CREATE TABLE pinky_promises (
    id               UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    proposal_id      UUID NOT NULL REFERENCES proposals(id) ON DELETE CASCADE,
    conversation_id  UUID NOT NULL REFERENCES conversations(id) ON DELETE CASCADE,
    -- "A": the proposer / initiator.
    user_a_id        UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    -- "B": the interested user, who confirms.
    user_b_id        UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    status           TEXT NOT NULL DEFAULT 'pending_b_confirm'
                          CHECK (status IN ('pending_b_confirm', 'confirmed', 'cancelled',
                                            'completed')),
    confirmed_at     TIMESTAMPTZ,
    created_at       TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- At most one live (pending or confirmed) PinkyPromise per Conversation.
-- A cancelled/completed one does not block a fresh attempt.
CREATE UNIQUE INDEX pinky_promises_one_live_per_conversation
    ON pinky_promises (conversation_id)
    WHERE status IN ('pending_b_confirm', 'confirmed');

-- Support the 3-cap count, which filters by status and either party id.
CREATE INDEX pinky_promises_user_a_status_idx ON pinky_promises (user_a_id, status);
CREATE INDEX pinky_promises_user_b_status_idx ON pinky_promises (user_b_id, status);

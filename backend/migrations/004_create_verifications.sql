-- 004_create_verifications.sql
-- Verification: one row per attempt. Profile.verified flips to true on the
-- first 'pass' (handled in the application layer, in the same transaction
-- as the decision update).

CREATE TABLE verifications (
    id                      UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id                 UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    submitted_at            TIMESTAMPTZ NOT NULL DEFAULT now(),
    liveness_session_id     TEXT,
    liveness_score          REAL,
    face_match_score        REAL,
    decision                TEXT NOT NULL DEFAULT 'pending'
                                CHECK (decision IN ('pending', 'pass', 'fail')),
    decided_at              TIMESTAMPTZ
);

-- Latest-attempt-per-user lookup, used by GET /verification/status.
CREATE INDEX verifications_user_submitted_idx ON verifications (user_id, submitted_at DESC);

-- 007_create_conversations_messages.sql
-- Conversation + Message (CUJ #4). A Conversation is opened when an
-- `interested` swipe lands -- that write is folded into the swipe INSERT
-- in src/services/SwipeService.cc so the two can never diverge. Messages
-- are the text/voice entries within a Conversation.
--
-- Expiry is COMPUTED ON READ from `created_at` + the ordered message list
-- (see src/services/ConversationExpiry.cc and the entities doc's
-- Section 5). There is deliberately NO ticking `expires_at` column and no
-- background sweep here: `status = 'expired'` exists in the CHECK only so
-- a future cleanup job can use it, but it is never what the countdown
-- reads from.

CREATE TABLE conversations (
    id                  UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    proposal_id         UUID NOT NULL REFERENCES proposals(id) ON DELETE CASCADE,
    -- "A": the proposal's creator.
    proposer_user_id    UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    -- "B": the user who swiped `interested`.
    interested_user_id  UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    last_activity_at    TIMESTAMPTZ NOT NULL,
    -- NULL until the first message is posted.
    last_sender_id      UUID REFERENCES users(id) ON DELETE SET NULL,
    status              TEXT NOT NULL DEFAULT 'active'
                             CHECK (status IN ('active', 'pinky_promised', 'expired')),
    created_at          TIMESTAMPTZ NOT NULL DEFAULT now(),

    -- One Conversation per (proposal, B) -- mirrors the one-swipe-per-
    -- (proposal, user) rule in 006_create_swipes.sql.
    CONSTRAINT conversations_one_per_proposal_interested UNIQUE (proposal_id, interested_user_id)
);

-- GET /v1/conversations lists a caller's conversations (caller is the
-- proposer OR the interested user), most-recent activity first.
CREATE INDEX conversations_proposer_idx
    ON conversations (proposer_user_id, last_activity_at DESC);
CREATE INDEX conversations_interested_idx
    ON conversations (interested_user_id, last_activity_at DESC);

CREATE TABLE messages (
    id               UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    conversation_id  UUID NOT NULL REFERENCES conversations(id) ON DELETE CASCADE,
    sender_user_id   UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    type             TEXT NOT NULL CHECK (type IN ('text', 'voice')),
    content_or_url   TEXT NOT NULL,
    created_at       TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Ordered reads of a conversation's messages -- also the exact input the
-- computed-expiry formula walks.
CREATE INDEX messages_conversation_created_idx
    ON messages (conversation_id, created_at);

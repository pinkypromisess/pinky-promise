-- 002_create_profiles.sql
-- Profile: 1:1 with User. Fields per docs/pinky-promise-entities-api.md.
-- photo_urls[] is normalized into its own table (003) rather than an array
-- column so PATCH /profile/photos can add/remove individual photos by a
-- stable id instead of matching on URL string.

CREATE TABLE profiles (
    user_id             UUID PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    name                TEXT NOT NULL,
    sex                 TEXT NOT NULL
                            CHECK (sex IN ('male', 'female', 'non_binary', 'other')),
    age                 INTEGER NOT NULL
                            CHECK (age BETWEEN 18 AND 120),
    need_to_know_text   TEXT NOT NULL,
    occupation          TEXT,
    relationship_status TEXT
                            CHECK (relationship_status IN (
                                'single', 'married', 'married_open',
                                'married_separated', 'divorced', 'widowed'
                            )),
    verified            BOOLEAN NOT NULL DEFAULT false,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT now()
);

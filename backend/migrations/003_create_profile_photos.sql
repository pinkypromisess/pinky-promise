-- 003_create_profile_photos.sql
-- Individual profile photos. Profile requires >= 6 at any given time; that
-- minimum is enforced in the application layer (on every insert/delete),
-- not via a DB constraint, since Postgres can't express "count >= 6" as a
-- simple CHECK across rows.

CREATE TABLE profile_photos (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id     UUID NOT NULL REFERENCES profiles(user_id) ON DELETE CASCADE,
    url         TEXT NOT NULL,
    position    INTEGER NOT NULL,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),

    CONSTRAINT profile_photos_user_position_unique UNIQUE (user_id, position)
);

CREATE INDEX profile_photos_user_id_idx ON profile_photos (user_id);

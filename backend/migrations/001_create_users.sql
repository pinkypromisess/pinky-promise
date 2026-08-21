-- 001_create_users.sql
-- User: auth/account only, no dating-specific fields.

CREATE EXTENSION IF NOT EXISTS pgcrypto;

CREATE TABLE users (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    email       TEXT UNIQUE,
    phone       TEXT UNIQUE,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),

    CONSTRAINT users_email_or_phone_present CHECK (email IS NOT NULL OR phone IS NOT NULL)
);

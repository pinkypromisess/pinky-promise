# Pinky Promise — Entities & API Surface (v0.6)

Derived from the confirmed CUJs: Profile is reusable, Pinky Promise requires mutual
confirm, strictly 1:1 per event (with multiple parallel Conversations allowed until
one confirms), occupation and relationship status are optional to fill AND optional
per-proposal reveals, verification is a fully automated vendor-API check gating
Hearts and Proposal posting (not browsing), profile edits are a live reference and
any photo change forces re-verification, Proposals can be deleted at any point
(cascading to cancel dependent Conversations/PinkyPromises/reminders), the
3-concurrent-Pinky-Promise cap is checked against both parties, and Block/Report
are basic MVP features (not deferred).

## Entities

### User
`id, email/phone, created_at`
Auth/account only — no dating-specific fields here.

### Profile (1:1 with User)
`user_id, name, sex, age, need_to_know_text, photo_urls[] (min 6), occupation (nullable), relationship_status (nullable), verified (bool, default false), created_at`
Reused across every Proposal. `name/sex/age/need_to_know_text/photo_urls` are required to create a profile at all (photo count is a live minimum, not a one-time gate — dropping below 6 after removing a photo should block further action same as never having reached 6). `occupation` and `relationship_status` may be left null.

### Verification
`id, user_id, submitted_at, liveness_score, face_match_score, decision [pass | fail], decided_at`
One row per attempt (a user can retry after a fail). `Profile.verified` flips to true on the first `pass`. No manual review step — `decision` is set synchronously or via vendor webhook based on a confidence threshold, never queued for a person to look at.

### Proposal
`id, creator_user_id, activity_text, event_time, location (lat/lng + address text), payment_type [split | host_treats | guest_treats | tbd], looking_for_text, revealed_fields[] (subset of {occupation, relationship_status}, default empty), status [active | pinky_promised | cancelled | expired], created_at`
One creator, but see open question below on whether one Proposal can have several parallel Conversations before one converts to a Pinky Promise.

**Visibility rule enforced at the API layer, not the DB:** any endpoint returning a Profile alongside a Proposal (feed card) excludes `occupation` and `relationship_status` unless each is present in that specific proposal's `revealed_fields`. A bare profile view with no Proposal context (e.g. CUJ #4, A viewing B after a match) always excludes both — there's no proposal to have revealed anything. This means "public profile" isn't a single fixed shape — it's computed per-context, which is worth a single shared serializer function so the exclusion logic lives in exactly one place rather than being re-implemented per endpoint.

### Swipe
`id, proposal_id, swiper_user_id, action [interested | pass], created_at`
Unique on `(proposal_id, swiper_user_id)`. This is what CUJ #3's Heart/X writes to. `pass` requires nothing; `interested` requires `Profile.verified = true` and counts against the "10 interested/day" rule.

### Conversation
`id, proposal_id, proposer_user_id (A), interested_user_id (B), last_activity_at, last_sender_id, status [active | pinky_promised | expired], created_at`
Expiry is **computed, not stored as a countdown** — see note below.

### Message
`id, conversation_id, sender_user_id, type [text | voice], content_or_url, created_at`

### PinkyPromise
`id, proposal_id, conversation_id, user_a_id, user_b_id, status [pending_b_confirm | confirmed | cancelled | completed], confirmed_at, created_at`
Created in `pending_b_confirm` when A taps the button; flips to `confirmed` when B taps back (confirmed as mutual — required, not optional). Conversation's un-timed state only kicks in once `status = confirmed`, not at the `pending_b_confirm` stage — so if B never confirms, the normal expiry clock keeps running. This is what the "3 concurrent" rule counts against (`status = confirmed AND event_time > now`).

### CheckIn (P2, stub for MVP)
`id, pinky_promise_id, user_id, did_meet (bool), feedback_text, created_at`

### Block
`id, blocker_user_id, blocked_user_id, created_at`
Symmetric in effect even though stored as one directional row — feed queries and Conversation-start checks treat `(A,B)` and `(B,A)` the same. Creating a Block cascades: closes any active Conversation between the pair, and cancels any confirmed PinkyPromise between them (same cascade as CUJ #8's delete).

### Report
`id, reporter_user_id, target_type [profile | proposal | conversation], target_id, reason [harassment | fake_profile | inappropriate_content | safety_concern | other], details_text, status [open | reviewed], created_at`
Creating a Report also creates a Block (reporter → reported user) as a side effect. No automated action beyond that for MVP — `status` stays `open` until you review it manually; there's no queue-processing job.

---

## API surface by CUJ

**CUJ #1 — Create Profile**
- `PUT /profile` — create/update profile (name, sex, age, "need to know", photos required; occupation, relationship_status optional). This same endpoint serves CUJ #7 (edit) — there's no separate create-vs-edit distinction needed, since Profile is 1:1 with User and always exists once created.
- `PATCH /profile/photos` — add/replace/remove photos; server rejects if resulting count < 6; on success, sets `verified = false` unconditionally (any photo mutation invalidates verification, per CUJ #7)
- `GET /profile/me`

**CUJ #2 — Verification**
- `POST /verification` — submit live-capture image/video; calls vendor API (e.g. Rekognition Face Liveness + CompareFaces against the 6 profile photos), returns `pass`/`fail` synchronously or via webhook; on `pass`, sets `Profile.verified = true`
- `GET /verification/status` — for polling if the vendor call is async

**CUJ #3 — Browse & swipe**
- `GET /proposals/feed` — candidates: not own, not already swiped, active status, **excludes any creator the requester has blocked or is blocked by**. MVP ranking = distance + recency, nothing smarter. No verification required to browse or pass.
- `POST /proposals/{id}/swipe` — `{action: interested|pass}`; `interested` returns 403 if `Profile.verified = false`, and enforces 10/day cap
- `POST /proposals` — create proposal; returns 403 if `Profile.verified = false` or photo count < 6

**CUJ #4 — Match & converse**
- `GET /conversations` / `GET /conversations/{id}` — include a server-computed `expires_at` in the response, not a stored ticking value (see below)
- `POST /conversations/{id}/messages` — `{type, content}`
- `POST /conversations/{id}/pinky-promise` — A initiates → creates `PinkyPromise(pending_b_confirm)`
- `POST /pinky-promises/{id}/confirm` — B confirms → status `confirmed`; server checks the 3-concurrent cap against **both** A and B, rejecting if either is already at 3. On success: `Proposal.status → pinky_promised`, and any other open `Conversation`s on that Proposal are closed (per CUJ #4's parallel-conversation rule).

**CUJ #5 — Reminder**
- No user-facing endpoint. A scheduled job queries `PinkyPromise(status=confirmed)` joined to `Proposal.event_time` and fires push notifications ~1hr out. This is the one piece that genuinely needs a cron/worker, everything else in this app can be computed on read.

**CUJ #6 — Check-in (P2)**
- `POST /pinky-promises/{id}/check-in` — `{did_meet, feedback_text}`

**CUJ #8 — Delete Proposal**
- `DELETE /proposals/{id}` — allowed regardless of status, including with a confirmed PinkyPromise. Cascades server-side in one transaction: `Proposal.status → cancelled`, any `Conversation(status=active) → expired`, `PinkyPromise(status=confirmed) → cancelled`, scheduled reminder job for that PinkyPromise cancelled. **Whether this also fires a notification to B is the open item noted in CUJ #8 — the endpoint contract doesn't change either way, just whether a notification side-effect fires inside the same transaction.**

**CUJ #9 — Recreate Proposal**
- No new endpoint — same `POST /proposals` as CUJ #3/#1, called again after a delete.

**CUJ #10 — Block & Report**
- `POST /blocks` — `{blocked_user_id}`; cascades close active Conversation + cancel confirmed PinkyPromise between the pair, same transaction pattern as `DELETE /proposals/{id}`
- `POST /reports` — `{target_type, target_id, reason, details_text}`; creates the Report row and, as a side effect, a Block (reporter → target's owning user)

---

## Conversation expiry — computed, not a background timer

Rather than running a job that ticks every conversation's countdown, store just the facts (`created_at`, `last_activity_at`, `last_sender_id`, per-sender first-reply markers) and **compute** `expires_at` on read using the rules you specified: 12hr base after A's first reply, +1min per same-sender repeat, +2hr per first-reply-after-silence from the other side, capped at 3 days total, removed entirely once `status = pinky_promised`. This avoids a whole class of scheduled-job infrastructure for something that's really just a formula over a few timestamps — a background sweep only needs to run periodically to flip `status → expired` for cleanup, not to maintain the countdown itself.

---

## Stub vs. real for MVP

| Piece | MVP version | Real version (later) |
|---|---|---|
| Feed ranking | distance + recency | learned ranking, preference matching |
| "Looking for" field | free text, unstructured | structured filters / embedding match |
| Photo/identity verification | automated vendor API (liveness + face match), threshold-based, no human review | possibly add manual review for edge cases, fraud-signal layering (device fingerprinting etc.) |
| Conflict handling (CUJ #6) | not built | dispute flow |
| Reminders | single push at -1hr | multi-channel, configurable |

---

## Open questions for next pass
1. Location: exact address, or approximate area until Pinky Promise is confirmed (safety-relevant for a meet-in-person app)?
2. When a Proposal with a confirmed Pinky Promise is deleted (CUJ #8) or a Block cancels one (CUJ #10), should B receive a notification (and/or should A be required to give a reason)? Currently the cascade is silent in both cases.

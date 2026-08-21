# Pinky Promise — Core User Journeys (v0.8, confirmed)

## CUJ #1: User creates a Profile

**Required to create a profile / start browsing:**
1. Name *(required)*
2. Sex *(required)*
3. Age *(required)*
4. "One thing you absolutely need to know about me" *(required, free text)*
5. Minimum **6 selfie photos** *(replaceable later — the requirement is a photo count at any given time, not a one-time upload)*

**Optional, can be left blank:**
6. Relationship status: single / married / married (open) / married (separated) / divorced / widowed
7. Occupation

**Proposal-specific fields** *(entered when posting a Proposal — see CUJ #2 for the verification gate on this)*
- Activity: "I would love to [ ______ ]"
- Time
- Location
- Payment: split / I'm treating / we'll figure it out / you're treating
- Who they're looking for: "I'm looking for someone who [ ______ ] to join me"

**Visibility rules (confirmed):**
- The Proposal card always shows the proposal-specific fields plus profile fields **1, 2, 3, 4, 5** (name, sex, age, "need to know," photos) — i.e. exactly the required-field set. This isn't a coincidence: **anything optional to fill is also never mandatory to display**, and vice versa.
- The creator can optionally choose to also reveal **relationship status and/or occupation (6, 7)** on that specific proposal, if they filled them in.

---

## CUJ #2: Selfie Verification

User takes a live selfie via in-app camera (not an upload), matched against their uploaded photos to confirm they're a real match for the person pictured — same pattern as Hinge's photo verification.

**Gating (confirmed):**
- **Browsing** the feed and passing (X) — allowed while unverified.
- **Swiping "interested" (Heart)** — requires verification.
- **Posting a Proposal** — requires verification.

A user can create a profile and look around immediately, but hits a verification prompt the first time they try to Heart something or post a Proposal.

**Implementation note:** verification is fully automated, no human review — a live capture is scored for liveness and face-match against the 6 profile photos via a vendor API (e.g. AWS Rekognition Face Liveness + CompareFaces), and `verified` flips based on a confidence threshold. Below threshold, the user is prompted to retry rather than queued for manual review.

---

## CUJ #3: User browses Proposals

Any user can swipe through Proposals. Each one renders as a clean, whiteboard-style card showing everything entered in CUJ #1, per the visibility rules above.

Actions per card:
- **X** — not interested
- **Heart** — interested *(requires verification — see CUJ #2)*

**Card layout (confirmed):** photos and info fields alternate in a single vertical scroll within one card (photo → info block → photo → info block, etc.) rather than a separate horizontal photo carousel plus a text section — one continuous scroll axis per card.

**Interaction model (confirmed):** X and Heart are fixed/sticky at the bottom of the screen throughout the scroll, so the user can act at any point without scrolling to the end. Advancing to the next Proposal only happens via X or Heart, never via a swipe gesture — this deliberately avoids any conflict between "scroll within this card" and "swipe to the next card," since there's no competing swipe-to-advance gesture to conflict with.

---

## CUJ #4: A user expresses interest

A Proposal can have **multiple simultaneous Conversations** — several different people can Heart the same Proposal and each opens its own thread with A. The Proposal stays open to new Hearts/Conversations right up until one of them results in a **fully confirmed** Pinky Promise (both A and B tapped confirm).

**The moment one Conversation reaches a confirmed Pinky Promise:**
- The Proposal closes to new interest (no longer appears in anyone's feed, can't be Hearted).
- Any *other* still-open Conversations on that Proposal are closed too — the event now has its confirmed partner, and the strict 1:1 rule means no second commitment is possible. (This follows directly from the 1:1 rule rather than being a separate decision, but flagging it since it's a real behavior change for anyone mid-conversation with A when this happens.)
- **These other users are shown a reason** ("this event has been filled") rather than the Conversation just silently disappearing — this is a system-caused closure, not a personal rejection, so explaining it is safe and helpful. Contrast with CUJ #11 (Unmatch) and CUJ #10 (Block), where closures stay silent on purpose.

When **User B** taps "interested" on **User A**'s proposal:

From A's side:
- A conversation thread is created between A and B (text + voice messages supported)
- A can view B's profile — fields 1, 2, 3, 4, 5 only (relationship status and occupation excluded — this is a bare profile view, not tied to any Proposal, so no per-proposal reveal applies here)
- A **cannot** view any proposal B may have posted
- A new **"Pinky Promise"** button appears on the conversation — A tapping it signals intent to commit to B for this event
- **B must then confirm** (a single tap, e.g. "I'm in") before the commitment is finalized — A alone cannot lock it in. The confirm is rejected with an error if **either** A or B is already at the 3-concurrent-Pinky-Promise cap.
- The conversation shows a visible "life remaining" countdown

**Conversation lifecycle:**
- If A never replies, the conversation expires **3 days** after creation.
- Once A sends a first reply, the countdown resets: **initial life = 12 hours** from that message.
- If A sends several messages in a row without a reply from B, each message after the first only adds **+1 minute** (prevents stalling the clock solo).
- Each time the *other* side sends their **first** message after a silence, it adds **+2 hours**.
- Total lifespan is capped at **3 days from conversation creation**, regardless of activity.
- Once **both A and B have confirmed** the Pinky Promise, the conversation no longer expires on this timer. More rules apply in CUJ #6.

---

## CUJ #5: Reminder

**1 hour before** a Pinky-Promised event's scheduled time, both users receive a reminder notification.

---

## CUJ #6: Post-event check-in (P2 for MVP)

After the event's scheduled time has passed, both users are asked whether they actually met, and optionally for feedback on the experience. Conflict handling (e.g. one says yes, one says no) is deferred past MVP.

---

## CUJ #7: Edit Profile

User can update any profile field at any time — name, sex, age, "need to know," occupation, relationship status, and photos.

- Photos can be added, replaced, or removed, but a save is rejected if it would drop the count below 6.
- **Live reference, not a snapshot:** edits appear immediately everywhere the profile is shown — active Proposal cards, ongoing Conversations, matched profile views. There's no frozen copy from posting time.
- **Any photo change invalidates verification:** adding, replacing, or removing any photo sets `verified = false` immediately, since the original verification was scored against that specific photo set. This only blocks *future* Hearts/Proposal-posting — it does not retroactively affect Conversations or Pinky Promises already confirmed before the edit.

---

## CUJ #8: Delete Proposal

Creator can delete a Proposal at any time, **including one with a confirmed Pinky Promise.**

- Deleting cascades: any open Conversations on that Proposal close, the associated PinkyPromise (if any) is cancelled, the scheduled reminder is cancelled, and check-in is skipped.
- **Resolved:** since this is a system/creator-driven closure (not a block/unmatch), the other party is shown a reason ("this event was removed by the host"), consistent with the same rule in CUJ #4.

---

## CUJ #9: Recreate Proposal

After deleting, the creator posts a new Proposal through the normal creation flow (CUJ #1) — no special behavior. *(Assumption: this is a fresh create, not a duplicate/template feature that pre-fills the deleted Proposal's fields — flag if you actually want a "repost" shortcut, since that's a small but real scoping difference.)*

---

## CUJ #10: Block & Report

Available from a Profile view, a Proposal card, or a Conversation.

**Block:**
- Immediately mutual: neither user sees the other's Proposals in their feed afterward, and no new Conversation can be started between them.
- Any active Conversation between them closes immediately.
- If a confirmed Pinky Promise exists between them, blocking cancels it the same way deleting a Proposal does (CUJ #8) — reminder cancelled, check-in skipped. Blocking is a stronger signal than a delete, so it should never leave an active commitment standing.

**Report:**
- User submits a reason (e.g. harassment, fake profile, inappropriate content, safety concern, other) plus optional free-text detail, against a Profile, Proposal, or Conversation.
- **Reporting a user automatically also blocks them** — the reporter is protected immediately rather than waiting on any review.
- Reports are **not manually reviewed in real time** for MVP — this is different from the selfie-verification "no manual step" constraint, since verification blocks every single user on every attempt, while reports are low-volume and async. For MVP, reports simply accumulate on the reported user's record for you to check periodically; no automated suspension logic yet.

---

## Global rules
- A user may swipe "interested" (Heart) on at most **10 proposals per day**.
- A user may have at most **3 active (non-expired) Pinky-Promised events** at once.

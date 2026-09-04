#pragma once

#include <chrono>
#include <string>

namespace notifications
{
// Abstracts the push-notification vendor (FCM/APNs or similar) behind an
// interface so ReminderService (CUJ #5: "1 hour before a Pinky-Promised
// event's scheduled time, both users receive a reminder notification")
// never talks to a vendor SDK directly, and a real implementation can be
// swapped in later without touching call sites -- same pattern as
// verification::FaceVerificationProvider and storage::GcsUploadUrlProvider.
//
// Only StubReminderProvider is implemented for now -- no real push
// infrastructure exists in this repo/environment to reach or verify
// end-to-end, same situation as verification::RekognitionFaceVerification-
// Provider (real implementation deferred).
class ReminderProvider
{
  public:
    virtual ~ReminderProvider() = default;

    // Sends one reminder to `userId` for the Pinky-Promised event
    // identified by `pinkyPromiseId`. `activityText` and `eventTime` are
    // passed through so the notification copy can describe what/when
    // without a second lookup by the provider. Called once per user (both
    // user_a and user_b get their own call) by
    // ReminderService::fireDueReminders().
    //
    // No return value: delivery is fire-and-forget from the caller's
    // perspective (no delivery receipts modeled for MVP) -- it is
    // ReminderService's send-time re-check of PinkyPromise/Proposal state,
    // not a callback from here, that determines whether a `reminders` row
    // is recorded as 'sent'.
    virtual void sendReminder(const std::string &userId,
                               const std::string &pinkyPromiseId,
                               const std::string &activityText,
                               std::chrono::system_clock::time_point eventTime) = 0;
};

}  // namespace notifications

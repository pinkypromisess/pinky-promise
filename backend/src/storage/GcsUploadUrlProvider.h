#pragma once

#include <string>

namespace storage
{
struct SignedUpload
{
    // A time-limited URL the client PUTs the photo bytes to directly. Must
    // be used with the exact Content-Type passed to createUploadUrl() —
    // real implementations bind Content-Type into the signature, so a
    // mismatched header makes GCS reject the PUT.
    std::string uploadUrl;

    // The object's permanent URL, unaffected by uploadUrl's expiry. This is
    // what gets stored as a Profile photo URL once the client confirms the
    // upload succeeded (see PUT /profile / PATCH /profile/photos).
    std::string objectUrl;

    // ISO 8601 UTC, e.g. "2026-08-20T19:47:00Z".
    std::string expiresAt;
};

// Abstracts generating a signed, time-limited upload URL for a photo
// object, so the real IAM-signBlob-backed implementation
// (GcsSignBlobUploadUrlProvider) can be swapped for a deterministic stub
// in tests and local dev — same pattern as
// verification::FaceVerificationProvider.
class GcsUploadUrlProvider
{
  public:
    virtual ~GcsUploadUrlProvider() = default;

    // `objectPath` is the full bucket-relative object path, already
    // computed server-side by the caller (see PhotoUploadService) — never
    // derived from client input. `contentType` is the exact Content-Type
    // the client's PUT to the returned uploadUrl must use.
    virtual SignedUpload createUploadUrl(const std::string &objectPath,
                                          const std::string &contentType) = 0;
};

}  // namespace storage

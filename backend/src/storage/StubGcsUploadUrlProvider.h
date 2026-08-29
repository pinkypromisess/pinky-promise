#pragma once

#include <string>

#include "GcsUploadUrlProvider.h"

namespace storage
{
// Deterministic fake: builds plausible-looking URLs with no real
// signature and performs no network I/O. Used for local development and
// tests so the app runs end-to-end without GCP credentials — mirrors
// verification::StubFaceVerificationProvider. The returned uploadUrl
// cannot actually be PUT to; nothing here talks to real GCS.
class StubGcsUploadUrlProvider : public GcsUploadUrlProvider
{
  public:
    explicit StubGcsUploadUrlProvider(std::string bucket = "stub-profile-photos-bucket");

    SignedUpload createUploadUrl(const std::string &objectPath,
                                  const std::string &contentType) override;

  private:
    std::string bucket_;
};

}  // namespace storage

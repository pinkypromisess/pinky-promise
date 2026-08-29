#pragma once

#include <trantor/net/EventLoopThread.h>

#include <chrono>
#include <mutex>
#include <optional>
#include <string>

#include "GcsUploadUrlProvider.h"

namespace storage
{
// Real implementation: generates a V4-signed GCS upload URL using the
// running Cloud Run service's own identity via the IAM Credentials API's
// signBlob method — never a downloaded service account key.
//
// Two GCP-only capabilities make this work, both of which mean this class
// is only usable when actually running on Cloud Run (or anywhere else
// with the same metadata server + IAM setup):
//  1. This service's own access token and service account email are read
//     from the instance metadata server (http://metadata.google.internal).
//  2. The IAM Credentials API is asked to sign the V4 string-to-sign
//     (see GcsV4Signing.h) on this service account's own behalf, which
//     requires the service account to hold
//     roles/iam.serviceAccountTokenCreator on itself — the standard
//     Cloud Run self-signing setup.
//
// main.cc wires up StubGcsUploadUrlProvider by default; this class is
// built and ready but not the default, the same way this module already
// treats verification::RekognitionFaceVerificationProvider — nothing in
// this repo can reach the real metadata server or IAM API to verify it
// end-to-end, so it needs manual verification once actually deployed.
class GcsSignBlobUploadUrlProvider : public GcsUploadUrlProvider
{
  public:
    explicit GcsSignBlobUploadUrlProvider(std::string bucket);

    SignedUpload createUploadUrl(const std::string &objectPath,
                                  const std::string &contentType) override;

  private:
    // Outbound HTTP calls run on this dedicated background loop, not the
    // app's own IO loops — createUploadUrl() blocks the calling (IO)
    // thread on a promise/future while the actual network work and its
    // callbacks happen entirely on ioThread_'s loop, so there's no risk
    // of a thread waiting on a callback that needs that same thread to
    // run (the exact deadlock this pattern is built to avoid).
    trantor::EventLoopThread ioThread_;

    std::string bucket_;

    std::mutex cacheMutex_;
    std::optional<std::string> cachedServiceAccountEmail_;
    std::optional<std::string> cachedAccessToken_;
    std::chrono::system_clock::time_point cachedAccessTokenExpiry_;

    std::string serviceAccountEmail();
    std::string accessToken();
    std::string signBlobBase64(const std::string &serviceAccountEmail,
                                const std::string &payloadBase64);
};

}  // namespace storage

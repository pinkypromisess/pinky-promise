#include "StubGcsUploadUrlProvider.h"

#include <chrono>

#include "TimeUtils.h"

namespace storage
{
namespace
{
constexpr std::chrono::seconds kTtl{900};  // ~15 minutes, matching the real provider.
}

StubGcsUploadUrlProvider::StubGcsUploadUrlProvider(std::string bucket) : bucket_(std::move(bucket))
{
}

SignedUpload StubGcsUploadUrlProvider::createUploadUrl(const std::string &objectPath,
                                                        const std::string &contentType)
{
    const auto now = std::chrono::system_clock::now();
    const auto objectUrl = "https://storage.googleapis.com/" + bucket_ + "/" + objectPath;

    SignedUpload upload;
    upload.objectUrl = objectUrl;
    // Not a real signature — clearly marked, and there's nothing on the
    // other end to accept a PUT to this URL. Tests assert on the shape
    // (query params present, object path embedded) rather than treating
    // this as a working upload endpoint.
    upload.uploadUrl = objectUrl + "?stub-signed=1&stub-content-type=" + contentType;
    upload.expiresAt = formatIso8601Utc(now + kTtl);
    return upload;
}

}  // namespace storage

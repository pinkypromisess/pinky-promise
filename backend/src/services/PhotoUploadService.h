#pragma once

#include <json/json.h>

#include <memory>
#include <string>

#include "../storage/GcsUploadUrlProvider.h"

namespace services
{
struct PhotoUploadUrl
{
    std::string uploadUrl;
    std::string objectUrl;
    std::string expiresAt;

    Json::Value toJson() const;
};

// Generates a signed URL the client uploads a profile photo directly to
// GCS with (POST /profile/photos/upload-url). Owns the object-path naming
// convention (profile-photos/{user_id}/{uuid}.{ext}) — the extension is
// looked up from content_type via an allowlist, never taken from client
// input. storage::GcsUploadUrlProvider itself is a generic "sign this
// exact path" capability with no knowledge of profile photos specifically.
class PhotoUploadService
{
  public:
    explicit PhotoUploadService(std::shared_ptr<storage::GcsUploadUrlProvider> provider);

    // Throws services::ValidationFailedException if contentType isn't one
    // of the supported photo types.
    PhotoUploadUrl createUploadUrl(const std::string &userId, const std::string &contentType);

  private:
    std::shared_ptr<storage::GcsUploadUrlProvider> provider_;
};

}  // namespace services

#include "PhotoUploadService.h"

#include <drogon/utils/Utilities.h>

#include "ServiceErrors.h"
#include "../validation/PhotoUploadValidation.h"

namespace services
{
Json::Value PhotoUploadUrl::toJson() const
{
    Json::Value j;
    j["upload_url"] = uploadUrl;
    j["object_url"] = objectUrl;
    j["expires_at"] = expiresAt;
    return j;
}

PhotoUploadService::PhotoUploadService(std::shared_ptr<storage::GcsUploadUrlProvider> provider)
  : provider_(std::move(provider))
{
}

PhotoUploadUrl PhotoUploadService::createUploadUrl(const std::string &userId,
                                                    const std::string &contentType)
{
    const auto extension = validation::extensionForPhotoContentType(contentType);
    if (!extension)
    {
        throw ValidationFailedException(
            {{"UNSUPPORTED_CONTENT_TYPE",
              "content_type must be one of: image/jpeg, image/png, image/webp."}});
    }

    // Never client-supplied: user_id comes from the authenticated caller
    // (see controllers::getUserId), the id is a fresh server-generated
    // UUID, and the extension is looked up from an allowlist keyed on
    // content_type above.
    const std::string objectPath =
        "profile-photos/" + userId + "/" + drogon::utils::getUuid() + "." + *extension;

    const auto signedUpload = provider_->createUploadUrl(objectPath, contentType);

    PhotoUploadUrl result;
    result.uploadUrl = signedUpload.uploadUrl;
    result.objectUrl = signedUpload.objectUrl;
    result.expiresAt = signedUpload.expiresAt;
    return result;
}

}  // namespace services

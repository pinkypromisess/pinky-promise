#include "PhotoUploadValidation.h"

#include <unordered_map>

namespace validation
{
namespace
{
// Deliberately conservative for MVP — HEIC (common on iOS) isn't
// supported yet; the client is expected to convert before uploading.
// Widening this set later only requires adding an entry here.
const std::unordered_map<std::string, std::string> &supportedContentTypes()
{
    static const std::unordered_map<std::string, std::string> kMap = {
        {"image/jpeg", "jpg"},
        {"image/png", "png"},
        {"image/webp", "webp"},
    };
    return kMap;
}

}  // namespace

std::optional<std::string> extensionForPhotoContentType(const std::string &contentType)
{
    const auto &supported = supportedContentTypes();
    const auto it = supported.find(contentType);
    if (it == supported.end())
    {
        return std::nullopt;
    }
    return it->second;
}

}  // namespace validation

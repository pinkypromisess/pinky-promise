#pragma once

#include <optional>
#include <string>

namespace validation
{
// Supported Content-Type values for profile photo uploads, and the file
// extension used when deriving the GCS object path server-side — the
// client never supplies the extension directly (see
// services::PhotoUploadService).
std::optional<std::string> extensionForPhotoContentType(const std::string &contentType);

}  // namespace validation

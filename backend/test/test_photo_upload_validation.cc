#include <drogon/drogon_test.h>

#include "../src/validation/PhotoUploadValidation.h"

// Pure logic, no DB/HTTP — mirrors the level test_profile_validation.cc
// tests validation::ProfileValidation at.

using namespace validation;

// CHECKS: image/jpeg maps to the "jpg" extension
DROGON_TEST(JpegMapsToJpgExtension)
{
    auto ext = extensionForPhotoContentType("image/jpeg");
    REQUIRE(ext.has_value());
    CHECK(*ext == "jpg");
}

// CHECKS: image/png maps to the "png" extension
DROGON_TEST(PngMapsToPngExtension)
{
    auto ext = extensionForPhotoContentType("image/png");
    REQUIRE(ext.has_value());
    CHECK(*ext == "png");
}

// CHECKS: image/webp maps to the "webp" extension
DROGON_TEST(WebpMapsToWebpExtension)
{
    auto ext = extensionForPhotoContentType("image/webp");
    REQUIRE(ext.has_value());
    CHECK(*ext == "webp");
}

// CHECKS: an unsupported content type (e.g. HEIC, not yet supported) returns no extension
DROGON_TEST(UnsupportedContentTypeReturnsNullopt)
{
    CHECK(!extensionForPhotoContentType("image/heic").has_value());
    CHECK(!extensionForPhotoContentType("application/pdf").has_value());
}

// CHECKS: an empty content type returns no extension rather than matching anything
DROGON_TEST(EmptyContentTypeReturnsNullopt)
{
    CHECK(!extensionForPhotoContentType("").has_value());
}

#include <drogon/drogon_test.h>

#include <ctime>

#include "../src/storage/GcsV4Signing.h"

// Pure logic, no network. This is the one part of
// GcsSignBlobUploadUrlProvider anything in this repo CAN verify — the
// actual IAM signBlob call and the metadata-server calls it depends on
// need a real GCP environment, so getting this deterministic
// string-building right (and covered) is what stands in for end-to-end
// confidence until this is manually verified on a real deployment.

using namespace storage;
using namespace std::chrono;

namespace
{
// 2026-08-20T19:47:05Z
system_clock::time_point fixedTime()
{
    std::tm tmUtc{};
    tmUtc.tm_year = 2026 - 1900;
    tmUtc.tm_mon = 8 - 1;
    tmUtc.tm_mday = 20;
    tmUtc.tm_hour = 19;
    tmUtc.tm_min = 47;
    tmUtc.tm_sec = 5;
#if defined(_WIN32)
    return system_clock::from_time_t(_mkgmtime(&tmUtc));
#else
    return system_clock::from_time_t(timegm(&tmUtc));
#endif
}

V4SigningRequest baseRequest()
{
    V4SigningRequest request;
    request.bucket = "pinky-promise-photos";
    request.objectPath = "profile-photos/u-123/p-456.jpg";
    request.contentType = "image/jpeg";
    request.serviceAccountEmail = "runtime-sa@my-project.iam.gserviceaccount.com";
    request.signingTime = fixedTime();
    request.ttl = seconds(900);
    return request;
}

}  // namespace

// CHECKS: sha256Hex matches the well-known SHA-256 hash of the empty string
DROGON_TEST(Sha256HexMatchesKnownVector)
{
    CHECK(sha256Hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

// CHECKS: percentEncode leaves unreserved characters untouched
DROGON_TEST(PercentEncodeLeavesUnreservedCharactersAlone)
{
    CHECK(percentEncode("abcXYZ019-._~") == "abcXYZ019-._~");
}

// CHECKS: percentEncode escapes reserved/unsafe characters as uppercase-hex %XX, space included
DROGON_TEST(PercentEncodeEscapesReservedCharacters)
{
    CHECK(percentEncode("a b") == "a%20b");
    CHECK(percentEncode("a/b") == "a%2Fb");
    CHECK(percentEncode("a@b.com") == "a%40b.com");
}

// CHECKS: the string-to-sign starts with the algorithm name, the X-Goog-Date, and the credential scope, per GCS's V4 signing spec
DROGON_TEST(StringToSignHasExpectedStructure)
{
    const auto material = buildV4SigningMaterial(baseRequest());

    // GOOG4-RSA-SHA256\n20260820T194705Z\n20260820/auto/storage/goog4_request\n<hash>
    const std::string expectedPrefix =
        "GOOG4-RSA-SHA256\n20260820T194705Z\n20260820/auto/storage/goog4_request\n";
    REQUIRE(material.stringToSign.size() > expectedPrefix.size());
    CHECK(material.stringToSign.substr(0, expectedPrefix.size()) == expectedPrefix);

    // The final component is a 64-char hex SHA-256 digest.
    const auto hash = material.stringToSign.substr(expectedPrefix.size());
    CHECK(hash.size() == 64);
}

// CHECKS: the assembled URL (before a signature is appended) contains the bucket, the object path with slashes preserved, and every required X-Goog-* query param
DROGON_TEST(UrlWithoutSignatureContainsExpectedComponents)
{
    const auto material = buildV4SigningMaterial(baseRequest());

    CHECK(material.urlWithoutSignature.find("https://storage.googleapis.com/pinky-promise-photos/"
                                             "profile-photos/u-123/p-456.jpg") == 0);
    CHECK(material.urlWithoutSignature.find("X-Goog-Algorithm=GOOG4-RSA-SHA256") !=
          std::string::npos);
    CHECK(material.urlWithoutSignature.find("X-Goog-Date=20260820T194705Z") != std::string::npos);
    CHECK(material.urlWithoutSignature.find("X-Goog-Expires=900") != std::string::npos);
    CHECK(material.urlWithoutSignature.find("X-Goog-SignedHeaders=content-type%3Bhost") !=
          std::string::npos);
    CHECK(material.urlWithoutSignature.find(
              "X-Goog-Credential=runtime-sa%40my-project.iam.gserviceaccount.com%2F20260820%"
              "2Fauto%2Fstorage%2Fgoog4_request") != std::string::npos);
}

// CHECKS: query parameters appear in sorted-by-key order, as the canonical request format requires
DROGON_TEST(QueryParametersAreSortedByKey)
{
    const auto material = buildV4SigningMaterial(baseRequest());
    const auto algPos = material.urlWithoutSignature.find("X-Goog-Algorithm=");
    const auto credPos = material.urlWithoutSignature.find("X-Goog-Credential=");
    const auto datePos = material.urlWithoutSignature.find("X-Goog-Date=");
    const auto expPos = material.urlWithoutSignature.find("X-Goog-Expires=");
    const auto signedHeadersPos = material.urlWithoutSignature.find("X-Goog-SignedHeaders=");

    REQUIRE(algPos != std::string::npos);
    REQUIRE(credPos != std::string::npos);
    REQUIRE(datePos != std::string::npos);
    REQUIRE(expPos != std::string::npos);
    REQUIRE(signedHeadersPos != std::string::npos);

    CHECK(algPos < credPos);
    CHECK(credPos < datePos);
    CHECK(datePos < expPos);
    CHECK(expPos < signedHeadersPos);
}

// CHECKS: two requests differing only in content_type produce different string-to-sign values (content-type is bound into the signature)
DROGON_TEST(DifferentContentTypeChangesStringToSign)
{
    auto jpegRequest = baseRequest();
    auto pngRequest = baseRequest();
    pngRequest.contentType = "image/png";

    const auto jpegMaterial = buildV4SigningMaterial(jpegRequest);
    const auto pngMaterial = buildV4SigningMaterial(pngRequest);

    CHECK(jpegMaterial.stringToSign != pngMaterial.stringToSign);
}

// CHECKS: appendSignature adds the signature as the final query parameter, after everything already in the canonical query string
DROGON_TEST(AppendSignatureAddsFinalQueryParam)
{
    const auto result = appendSignature("https://example.com/x?a=1", "deadbeef");
    CHECK(result == "https://example.com/x?a=1&X-Goog-Signature=deadbeef");
}

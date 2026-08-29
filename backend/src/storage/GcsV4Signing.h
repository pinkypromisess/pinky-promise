#pragma once

#include <chrono>
#include <string>

// Pure, network-free implementation of GCS's "V4 signing process" for a
// PUT upload URL (https://cloud.google.com/storage/docs/authentication/signatures),
// split out from GcsSignBlobUploadUrlProvider specifically so it can be
// unit-tested without live GCP access — the actual signBlob network call
// is the one part of this flow nothing in this repo can verify
// end-to-end, so getting the deterministic string-building right (and
// covering it with tests) matters more than usual here.
namespace storage
{
struct V4SigningRequest
{
    std::string bucket;
    std::string objectPath;           // bucket-relative, e.g. "profile-photos/u/o.jpg"
    std::string contentType;          // bound into the signature as a required header
    std::string serviceAccountEmail;  // signer identity, from IAM
    std::chrono::system_clock::time_point signingTime;
    std::chrono::seconds ttl;
};

// Everything needed to (a) ask IAM to sign stringToSign and (b) assemble
// the final URL once that signature comes back.
struct V4SigningMaterial
{
    std::string stringToSign;
    std::string urlWithoutSignature;  // "https://host/resource?canonical_query_string"
};

V4SigningMaterial buildV4SigningMaterial(const V4SigningRequest &request);

// Appends "&X-Goog-Signature=<hex>" to complete the URL.
std::string appendSignature(const std::string &urlWithoutSignature, const std::string &signatureHex);

// RFC 3986 percent-encoding (unreserved set: A-Za-z0-9-._~); exposed for
// tests, not meant to be called outside this module otherwise.
std::string percentEncode(const std::string &value);

std::string sha256Hex(const std::string &data);

std::string hexEncode(const unsigned char *data, size_t len);

}  // namespace storage

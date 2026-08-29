#include "GcsV4Signing.h"

#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <vector>

#include "TimeUtils.h"

namespace storage
{
namespace
{
constexpr const char *kHost = "storage.googleapis.com";
constexpr const char *kAlgorithm = "GOOG4-RSA-SHA256";

// Preserves '/' as a literal path separator (GCS object names commonly
// contain it) while percent-encoding each segment individually.
std::string percentEncodePathSegments(const std::string &path)
{
    std::string out;
    size_t start = 0;
    while (start <= path.size())
    {
        const size_t slash = path.find('/', start);
        const auto segment =
            path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        out += percentEncode(segment);
        if (slash == std::string::npos)
        {
            break;
        }
        out.push_back('/');
        start = slash + 1;
    }
    return out;
}

}  // namespace

std::string hexEncode(const unsigned char *data, size_t len)
{
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i)
    {
        out.push_back(kHex[(data[i] >> 4) & 0xF]);
        out.push_back(kHex[data[i] & 0xF]);
    }
    return out;
}

std::string sha256Hex(const std::string &data)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;
    EVP_Digest(data.data(), data.size(), digest, &digestLen, EVP_sha256(), nullptr);
    return hexEncode(digest, digestLen);
}

std::string percentEncode(const std::string &value)
{
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value)
    {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~')
        {
            out.push_back(static_cast<char>(c));
        }
        else
        {
            out.push_back('%');
            out.push_back(kHex[(c >> 4) & 0xF]);
            out.push_back(kHex[c & 0xF]);
        }
    }
    return out;
}

V4SigningMaterial buildV4SigningMaterial(const V4SigningRequest &request)
{
    const std::string googDate = formatGoogDate(request.signingTime);
    const std::string dateStamp = formatGoogDateStamp(request.signingTime);
    const std::string credentialScope = dateStamp + "/auto/storage/goog4_request";
    const std::string googCredential = request.serviceAccountEmail + "/" + credentialScope;

    const std::string resource =
        "/" + percentEncode(request.bucket) + "/" + percentEncodePathSegments(request.objectPath);

    // Query params must be sorted by (encoded) key for the canonical
    // request to hash the way GCS expects.
    std::vector<std::pair<std::string, std::string>> queryParams = {
        {"X-Goog-Algorithm", kAlgorithm},
        {"X-Goog-Credential", googCredential},
        {"X-Goog-Date", googDate},
        {"X-Goog-Expires", std::to_string(request.ttl.count())},
        {"X-Goog-SignedHeaders", "content-type;host"},
    };
    std::sort(queryParams.begin(), queryParams.end());

    std::string canonicalQueryString;
    for (size_t i = 0; i < queryParams.size(); ++i)
    {
        if (i > 0)
        {
            canonicalQueryString += "&";
        }
        canonicalQueryString +=
            percentEncode(queryParams[i].first) + "=" + percentEncode(queryParams[i].second);
    }

    // Binding content-type here means the client's PUT must send exactly
    // this Content-Type header, or GCS rejects the signature — a cheap
    // extra guarantee that what gets uploaded matches what the object
    // path's extension (derived from this same content type) claims it
    // is.
    const std::string canonicalHeaders =
        "content-type:" + request.contentType + "\n" + "host:" + kHost + "\n";
    const std::string signedHeaders = "content-type;host";

    const std::string canonicalRequest = "PUT\n" + resource + "\n" + canonicalQueryString + "\n" +
                                          canonicalHeaders + "\n" + signedHeaders + "\n" +
                                          "UNSIGNED-PAYLOAD";

    const std::string stringToSign = std::string(kAlgorithm) + "\n" + googDate + "\n" +
                                      credentialScope + "\n" + sha256Hex(canonicalRequest);

    V4SigningMaterial material;
    material.stringToSign = stringToSign;
    material.urlWithoutSignature =
        "https://" + std::string(kHost) + resource + "?" + canonicalQueryString;
    return material;
}

std::string appendSignature(const std::string &urlWithoutSignature, const std::string &signatureHex)
{
    return urlWithoutSignature + "&X-Goog-Signature=" + signatureHex;
}

}  // namespace storage

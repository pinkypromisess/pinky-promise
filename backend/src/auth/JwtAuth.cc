#include "JwtAuth.h"

#include <openssl/crypto.h>
#include <openssl/hmac.h>

#include <chrono>
#include <json/json.h>

#include <drogon/utils/Utilities.h>

namespace auth
{
namespace
{
std::string stripBearerPrefix(const std::string &header)
{
    constexpr const char *kPrefix = "Bearer ";
    constexpr size_t kPrefixLen = 7;
    if (header.size() > kPrefixLen && header.compare(0, kPrefixLen, kPrefix) == 0)
    {
        return header.substr(kPrefixLen);
    }
    return header;
}

// JWT base64url segments have no padding and use '-'/'_' instead of '+'/'/'.
std::optional<std::string> base64UrlDecode(const std::string &segment)
{
    std::string standard = segment;
    for (auto &c : standard)
    {
        if (c == '-')
            c = '+';
        else if (c == '_')
            c = '/';
    }
    while (standard.size() % 4 != 0)
    {
        standard.push_back('=');
    }
    if (!drogon::utils::isBase64(standard))
    {
        return std::nullopt;
    }
    return drogon::utils::base64Decode(standard);
}

std::vector<std::string> splitJwt(const std::string &token)
{
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i <= token.size(); ++i)
    {
        if (i == token.size() || token[i] == '.')
        {
            parts.push_back(token.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

}  // namespace

std::optional<std::string> verifyAndExtractUserId(const std::string &bearerToken,
                                                    const std::string &hmacSecret)
{
    const std::string token = stripBearerPrefix(bearerToken);
    const auto parts = splitJwt(token);
    if (parts.size() != 3 || parts[0].empty() || parts[1].empty() || parts[2].empty())
    {
        return std::nullopt;
    }
    const auto &headerB64 = parts[0];
    const auto &payloadB64 = parts[1];
    const auto &signatureB64 = parts[2];

    auto headerJson = base64UrlDecode(headerB64);
    if (!headerJson)
    {
        return std::nullopt;
    }
    Json::Value header;
    Json::CharReaderBuilder builder;
    std::string parseErrors;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(
            headerJson->data(), headerJson->data() + headerJson->size(), &header, &parseErrors) ||
        header["alg"].asString() != "HS256")
    {
        return std::nullopt;
    }

    auto signature = base64UrlDecode(signatureB64);
    if (!signature)
    {
        return std::nullopt;
    }

    const std::string signingInput = headerB64 + "." + payloadB64;
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;
    if (HMAC(EVP_sha256(),
             hmacSecret.data(),
             static_cast<int>(hmacSecret.size()),
             reinterpret_cast<const unsigned char *>(signingInput.data()),
             signingInput.size(),
             digest,
             &digestLen) == nullptr)
    {
        return std::nullopt;
    }

    if (digestLen != signature->size() ||
        CRYPTO_memcmp(digest, signature->data(), digestLen) != 0)
    {
        return std::nullopt;
    }

    auto payloadJson = base64UrlDecode(payloadB64);
    if (!payloadJson)
    {
        return std::nullopt;
    }
    Json::Value payload;
    if (!reader->parse(payloadJson->data(),
                        payloadJson->data() + payloadJson->size(),
                        &payload,
                        &parseErrors))
    {
        return std::nullopt;
    }

    if (payload.isMember("exp") && payload["exp"].isNumeric())
    {
        const auto exp = static_cast<int64_t>(payload["exp"].asDouble());
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
        if (now >= exp)
        {
            return std::nullopt;
        }
    }

    if (!payload.isMember("sub") || !payload["sub"].isString() || payload["sub"].asString().empty())
    {
        return std::nullopt;
    }

    return payload["sub"].asString();
}

}  // namespace auth

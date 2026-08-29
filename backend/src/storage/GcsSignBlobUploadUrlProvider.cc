#include "GcsSignBlobUploadUrlProvider.h"

#include <drogon/HttpClient.h>
#include <drogon/HttpTypes.h>
#include <drogon/utils/Utilities.h>
#include <json/json.h>

#include <future>
#include <memory>
#include <stdexcept>
#include <vector>

#include "GcsV4Signing.h"
#include "TimeUtils.h"

namespace storage
{
namespace
{
constexpr std::chrono::seconds kTtl{900};  // ~15 minutes, per spec.
constexpr const char *kMetadataFlavorHeader = "Metadata-Flavor";
constexpr const char *kMetadataFlavorValue = "Google";

struct SyncHttpResult
{
    drogon::HttpStatusCode status = drogon::kUnknown;
    std::string body;
};

// Blocks the calling thread until the response arrives. `loop` must be a
// loop other than the calling thread's own (see the class comment on
// ioThread_ in the header) — otherwise the callback that fulfills `done`
// would need to run on the very thread that's blocked waiting for it.
SyncHttpResult syncHttpRequest(trantor::EventLoop *loop,
                                const std::string &baseUrl,
                                drogon::HttpMethod method,
                                const std::string &path,
                                const std::vector<std::pair<std::string, std::string>> &headers,
                                const std::string &body = "")
{
    auto client = drogon::HttpClient::newHttpClient(baseUrl, loop);
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(method);
    req->setPath(path);
    for (const auto &header : headers)
    {
        req->addHeader(header.first, header.second);
    }
    if (!body.empty())
    {
        req->setBody(body);
    }

    // Shared state is held by shared_ptr and captured BY VALUE into the
    // response callback: per CLAUDE.md, callbacks that outlive the current
    // stack frame must never capture locals or `this` by reference, with
    // no case-by-case exceptions. doneFuture.wait() below does keep this
    // frame alive until the callback runs, but the rule is deliberately
    // flat, so this is safe by construction rather than by argument.
    auto state = std::make_shared<SyncHttpResult>();
    auto done = std::make_shared<std::promise<void>>();
    auto doneFuture = done->get_future();
    client->sendRequest(
        req, [state, done](drogon::ReqResult reqResult, const drogon::HttpResponsePtr &resp) {
            if (reqResult == drogon::ReqResult::Ok && resp != nullptr)
            {
                state->status = resp->statusCode();
                state->body = std::string(resp->body());
            }
            done->set_value();
        });
    doneFuture.wait();

    if (state->status == drogon::kUnknown)
    {
        throw std::runtime_error("request to " + baseUrl + path + " failed at the transport level");
    }
    return *state;
}

Json::Value parseJsonOrThrow(const std::string &body, const std::string &context)
{
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::string parseErrors;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(body.data(), body.data() + body.size(), &parsed, &parseErrors))
    {
        throw std::runtime_error(context + " response was not valid JSON: " + parseErrors);
    }
    return parsed;
}

}  // namespace

GcsSignBlobUploadUrlProvider::GcsSignBlobUploadUrlProvider(std::string bucket)
  : ioThread_("gcs-upload-url-signer"), bucket_(std::move(bucket))
{
    ioThread_.run();
}

std::string GcsSignBlobUploadUrlProvider::serviceAccountEmail()
{
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (cachedServiceAccountEmail_)
        {
            return *cachedServiceAccountEmail_;
        }
    }

    auto result = syncHttpRequest(
        ioThread_.getLoop(),
        "http://metadata.google.internal",
        drogon::Get,
        "/computeMetadata/v1/instance/service-accounts/default/email",
        {{kMetadataFlavorHeader, kMetadataFlavorValue}});
    if (result.status != drogon::k200OK)
    {
        throw std::runtime_error("metadata server returned " + std::to_string(result.status) +
                                  " fetching the service account email");
    }

    std::lock_guard<std::mutex> lock(cacheMutex_);
    cachedServiceAccountEmail_ = result.body;
    return *cachedServiceAccountEmail_;
}

std::string GcsSignBlobUploadUrlProvider::accessToken()
{
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (cachedAccessToken_ && std::chrono::system_clock::now() < cachedAccessTokenExpiry_)
        {
            return *cachedAccessToken_;
        }
    }

    auto result = syncHttpRequest(
        ioThread_.getLoop(),
        "http://metadata.google.internal",
        drogon::Get,
        "/computeMetadata/v1/instance/service-accounts/default/token",
        {{kMetadataFlavorHeader, kMetadataFlavorValue}});
    if (result.status != drogon::k200OK)
    {
        throw std::runtime_error("metadata server returned " + std::to_string(result.status) +
                                  " fetching an access token");
    }

    const auto parsed = parseJsonOrThrow(result.body, "metadata server token");
    const std::string token = parsed["access_token"].asString();
    const auto ttl = std::chrono::seconds(parsed["expires_in"].asInt64());
    // Refresh a minute early rather than racing an exact expiry.
    const auto safeTtl = ttl > std::chrono::seconds(60) ? ttl - std::chrono::seconds(60) : ttl;

    std::lock_guard<std::mutex> lock(cacheMutex_);
    cachedAccessToken_ = token;
    cachedAccessTokenExpiry_ = std::chrono::system_clock::now() + safeTtl;
    return *cachedAccessToken_;
}

std::string GcsSignBlobUploadUrlProvider::signBlobBase64(const std::string &serviceAccountEmailValue,
                                                          const std::string &payloadBase64)
{
    Json::Value requestBody;
    requestBody["payload"] = payloadBase64;
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    const std::string requestJson = Json::writeString(writer, requestBody);

    auto result = syncHttpRequest(
        ioThread_.getLoop(),
        "https://iamcredentials.googleapis.com",
        drogon::Post,
        "/v1/projects/-/serviceAccounts/" + serviceAccountEmailValue + ":signBlob",
        {{"Authorization", "Bearer " + accessToken()}, {"Content-Type", "application/json"}},
        requestJson);
    if (result.status != drogon::k200OK)
    {
        throw std::runtime_error("IAM signBlob returned " + std::to_string(result.status) + ": " +
                                  result.body);
    }

    const auto parsed = parseJsonOrThrow(result.body, "IAM signBlob");
    return parsed["signedBlob"].asString();
}

SignedUpload GcsSignBlobUploadUrlProvider::createUploadUrl(const std::string &objectPath,
                                                            const std::string &contentType)
{
    const auto now = std::chrono::system_clock::now();
    const auto email = serviceAccountEmail();

    V4SigningRequest signingRequest;
    signingRequest.bucket = bucket_;
    signingRequest.objectPath = objectPath;
    signingRequest.contentType = contentType;
    signingRequest.serviceAccountEmail = email;
    signingRequest.signingTime = now;
    signingRequest.ttl = kTtl;

    const auto material = buildV4SigningMaterial(signingRequest);

    // signBlob takes and returns base64; the URL needs the raw signature
    // bytes hex-encoded, not the base64 IAM hands back.
    const auto payloadBase64 = drogon::utils::base64Encode(
        reinterpret_cast<const unsigned char *>(material.stringToSign.data()),
        material.stringToSign.size());
    const auto signatureBase64 = signBlobBase64(email, payloadBase64);
    const auto signatureRaw = drogon::utils::base64Decode(signatureBase64);
    const auto signatureHex = hexEncode(
        reinterpret_cast<const unsigned char *>(signatureRaw.data()), signatureRaw.size());

    SignedUpload upload;
    upload.uploadUrl = appendSignature(material.urlWithoutSignature, signatureHex);
    upload.objectUrl = "https://storage.googleapis.com/" + bucket_ + "/" + objectPath;
    upload.expiresAt = formatIso8601Utc(now + kTtl);
    return upload;
}

}  // namespace storage

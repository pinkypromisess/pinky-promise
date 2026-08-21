#include "TestHttpServer.h"

#include <drogon/HttpClient.h>
#include <drogon/drogon.h>
#include <openssl/hmac.h>

#include <chrono>
#include <cstring>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "TestDb.h"
#include "../src/AppContext.h"

namespace test_support
{
namespace
{
constexpr unsigned short kTestServerPort = 8199;
constexpr const char *kTestJwtSecret = "local-dev-insecure-secret";

std::shared_ptr<verification::StubFaceVerificationProvider> sharedProvider;

std::string base64UrlEncode(const std::string &data)
{
    return drogon::utils::base64Encode(
        reinterpret_cast<const unsigned char *>(data.data()), data.size(), /*urlSafe=*/true, /*padded=*/false);
}

// Not detached: left running and detached, this thread is still inside
// drogon::app().run()'s event loop when the process starts exiting after
// main() returns, and Drogon's EventLoop asserts if it's torn down from
// any thread other than the one actually running it ("It is forbidden to
// run loop on threads other than event-loop thread") — that fires during
// static destruction, after every test has already passed, so it looks
// like a crash rather than what it is. shutdownTestServer(), registered
// below via atexit, quits the loop from its own thread and joins before
// static teardown begins, avoiding that race.
std::thread *serverThread = nullptr;

void shutdownTestServer()
{
    if (serverThread == nullptr)
    {
        return;
    }
    drogon::app().getLoop()->queueInLoop([]() { drogon::app().quit(); });
    serverThread->join();
    delete serverThread;
    serverThread = nullptr;
}

}  // namespace

std::string ensureTestServerRunning()
{
    static std::string baseUrl;
    static std::once_flag started;

    std::call_once(started, [] {
        // Throws here (before the background thread starts) if the DB is
        // unreachable, which the caller's try/catch will see synchronously
        // — simpler than surfacing a failure from inside the background
        // thread.
        auto db = testDbClient();
        db->execSqlSync("SELECT 1");

        sharedProvider = std::make_shared<verification::StubFaceVerificationProvider>();
        app_context::init(db, sharedProvider);

        std::promise<void> ready;
        auto readyFuture = ready.get_future();
        serverThread = new std::thread([&ready]() {
            drogon::app().addListener("127.0.0.1", kTestServerPort);
            drogon::app().getLoop()->queueInLoop([&ready]() { ready.set_value(); });
            drogon::app().run();
        });
        readyFuture.wait();
        std::atexit(shutdownTestServer);

        baseUrl = "http://127.0.0.1:" + std::to_string(kTestServerPort);
    });

    return baseUrl;
}

std::shared_ptr<verification::StubFaceVerificationProvider> testServerVerificationProvider()
{
    return sharedProvider;
}

std::string signTestJwt(const std::string &userId)
{
    Json::Value header;
    header["alg"] = "HS256";
    header["typ"] = "JWT";

    Json::Value payload;
    payload["sub"] = userId;
    payload["exp"] = static_cast<Json::Int64>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count() +
        3600);

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    const std::string headerB64 = base64UrlEncode(Json::writeString(writer, header));
    const std::string payloadB64 = base64UrlEncode(Json::writeString(writer, payload));
    const std::string signingInput = headerB64 + "." + payloadB64;

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;
    HMAC(EVP_sha256(),
         kTestJwtSecret,
         static_cast<int>(std::strlen(kTestJwtSecret)),
         reinterpret_cast<const unsigned char *>(signingInput.data()),
         signingInput.size(),
         digest,
         &digestLen);
    const std::string signatureB64 =
        drogon::utils::base64Encode(digest, digestLen, /*urlSafe=*/true, /*padded=*/false);

    return signingInput + "." + signatureB64;
}

HttpTestResponse sendTestRequest(const std::string &baseUrl,
                                  drogon::HttpMethod method,
                                  const std::string &path,
                                  const std::string &bearerToken,
                                  const Json::Value *jsonBody)
{
    auto client = drogon::HttpClient::newHttpClient(baseUrl, drogon::app().getLoop());
    // newHttpJsonRequest sets the request's *typed* content-type field
    // (drogon::CT_APPLICATION_JSON), not just a raw header string. The
    // server's req->getJsonObject() reads that typed field, not the
    // header — setBody() + addHeader("Content-Type", ...) looks correct
    // but silently leaves the request looking like it has no JSON body at
    // all, which is exactly the bug that produced every PUT /profile in
    // this file's tests failing with 400 INVALID_BODY the first time this
    // was written.
    auto req = (jsonBody != nullptr) ? drogon::HttpRequest::newHttpJsonRequest(*jsonBody)
                                      : drogon::HttpRequest::newHttpRequest();
    req->setMethod(method);
    req->setPath(path);
    if (!bearerToken.empty())
    {
        req->addHeader("Authorization", "Bearer " + bearerToken);
    }

    HttpTestResponse result;
    std::promise<void> done;
    auto doneFuture = done.get_future();
    client->sendRequest(
        req, [&result, &done](drogon::ReqResult reqResult, const drogon::HttpResponsePtr &resp) {
            if (reqResult == drogon::ReqResult::Ok && resp != nullptr)
            {
                result.status = resp->statusCode();
                auto json = resp->getJsonObject();
                if (json)
                {
                    result.json = *json;
                }
            }
            done.set_value();
        });
    doneFuture.wait();

    if (result.status == drogon::kUnknown)
    {
        throw std::runtime_error("HTTP request to " + path + " failed at the transport level");
    }

    return result;
}

}  // namespace test_support

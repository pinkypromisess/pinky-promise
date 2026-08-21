#include "TestHttpFixtures.h"

#include "TestDb.h"
#include "TestHttpServer.h"

using namespace drogon;

namespace test_support
{
TestSession setUpTestSession()
{
    TestSession s;
    auto db = testDbClient();
    s.userId = createTestUser(db);
    s.baseUrl = ensureTestServerRunning();
    s.token = signTestJwt(s.userId);
    return s;
}

Json::Value jsonStringArray(const std::vector<std::string> &values)
{
    Json::Value arr(Json::arrayValue);
    for (const auto &v : values)
    {
        arr.append(v);
    }
    return arr;
}

Json::Value putProfileOverHttp(const TestSession &session, const std::vector<std::string> &photoUrls)
{
    Json::Value body;
    body["name"] = "Test User";
    body["sex"] = "female";
    body["age"] = 30;
    body["need_to_know_text"] = "I love testing.";
    body["photo_urls"] = jsonStringArray(photoUrls);

    auto resp = sendTestRequest(session.baseUrl, Put, "/v1/profile", session.token, &body);
    if (resp.status != k200OK)
    {
        throw std::runtime_error("PUT /v1/profile returned unexpected status " +
                                  std::to_string(resp.status));
    }
    return resp.json;
}

std::string verifyOverHttp(const TestSession &session)
{
    auto startResp = sendTestRequest(session.baseUrl, Post, "/v1/verification", session.token);
    if (startResp.status != k201Created)
    {
        throw std::runtime_error("POST /v1/verification returned unexpected status " +
                                  std::to_string(startResp.status));
    }
    auto statusResp =
        sendTestRequest(session.baseUrl, Get, "/v1/verification/status", session.token);
    if (statusResp.status != k200OK)
    {
        throw std::runtime_error("GET /v1/verification/status returned unexpected status " +
                                  std::to_string(statusResp.status));
    }
    return statusResp.json["decision"].asString();
}

bool getProfileVerifiedOverHttp(const TestSession &session)
{
    auto resp = sendTestRequest(session.baseUrl, Get, "/v1/profile/me", session.token);
    if (resp.status != k200OK)
    {
        throw std::runtime_error("GET /v1/profile/me returned unexpected status " +
                                  std::to_string(resp.status));
    }
    return resp.json["verified"].asBool();
}

}  // namespace test_support

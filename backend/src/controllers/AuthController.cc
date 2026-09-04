#include "AuthController.h"

#include "../AppContext.h"
#include "../auth/JwtAuth.h"
#include "../services/AuthServiceErrors.h"
#include "ControllerUtils.h"

using namespace drogon;

namespace controllers
{
namespace
{
// Single long-lived access token, no refresh-token mechanism -- a
// deliberate MVP simplification (see this module's brief). 30 days in
// seconds.
constexpr int64_t kTokenTtlSeconds = 30 * 24 * 60 * 60;

Json::Value authResponseBody(const std::string &userId)
{
    Json::Value j;
    j["token"] = auth::signJwt(userId, auth::signingSecret(), kTokenTtlSeconds);
    j["user_id"] = userId;
    return j;
}

}  // namespace

void AuthController::signup(const HttpRequestPtr &req,
                             std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        callback(errorResponse(k400BadRequest, "INVALID_BODY", "Request body must be JSON."));
        return;
    }

    const auto email = jsonBody->get("email", "").asString();
    const auto password = jsonBody->get("password", "").asString();

    try
    {
        const auto userId = app_context::authService().signup(email, password);
        callback(jsonResponse(authResponseBody(userId), k201Created));
    }
    catch (const services::AuthBadRequestException &e)
    {
        callback(errorResponse(k400BadRequest, e.code, e.what()));
    }
    catch (const services::AuthConflictException &e)
    {
        callback(errorResponse(k409Conflict, e.code, e.what()));
    }
}

void AuthController::login(const HttpRequestPtr &req,
                            std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        callback(errorResponse(k400BadRequest, "INVALID_BODY", "Request body must be JSON."));
        return;
    }

    const auto email = jsonBody->get("email", "").asString();
    const auto password = jsonBody->get("password", "").asString();

    try
    {
        const auto userId = app_context::authService().login(email, password);
        callback(jsonResponse(authResponseBody(userId), k200OK));
    }
    catch (const services::AuthUnauthorizedException &e)
    {
        callback(errorResponse(k401Unauthorized, e.code, e.what()));
    }
}

}  // namespace controllers

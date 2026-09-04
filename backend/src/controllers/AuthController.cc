#include "AuthController.h"

#include "../AppContext.h"
#include "../auth/JwtAuth.h"
#include "../services/AuthService.h"
#include "../services/AuthServiceErrors.h"
#include "ControllerUtils.h"

#include <functional>

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

// Shared by socialGoogle()/socialApple(): parses the body, calls
// `signupOrLogin`, and renders the 201/200 + token/user_id response or
// the 401 INVALID_SOCIAL_TOKEN failure. `signupOrLogin` is one of
// AuthService's two signupOrLoginWith*() methods, bound to the right
// provider by the caller.
void handleSocialLogin(
    const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &callback,
    const std::function<services::SocialLoginResult(const std::string &)> &signupOrLogin)
{
    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        callback(errorResponse(k400BadRequest, "INVALID_BODY", "Request body must be JSON."));
        return;
    }

    const auto idToken = jsonBody->get("id_token", "").asString();

    try
    {
        const auto result = signupOrLogin(idToken);
        const auto status = result.wasCreated ? k201Created : k200OK;
        callback(jsonResponse(authResponseBody(result.userId), status));
    }
    catch (const services::AuthUnauthorizedException &e)
    {
        callback(errorResponse(k401Unauthorized, e.code, e.what()));
    }
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

void AuthController::socialGoogle(const HttpRequestPtr &req,
                                   std::function<void(const HttpResponsePtr &)> &&callback)
{
    handleSocialLogin(req, callback, [](const std::string &idToken) {
        return app_context::authService().signupOrLoginWithGoogle(idToken);
    });
}

void AuthController::socialApple(const HttpRequestPtr &req,
                                  std::function<void(const HttpResponsePtr &)> &&callback)
{
    handleSocialLogin(req, callback, [](const std::string &idToken) {
        return app_context::authService().signupOrLoginWithApple(idToken);
    });
}

}  // namespace controllers

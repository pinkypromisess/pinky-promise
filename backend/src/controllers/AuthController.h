#pragma once

#include <drogon/HttpController.h>

namespace controllers
{
// Implements openapi.yaml's /auth/signup and /auth/login paths (Module
// F.1: email+password auth). Deliberately the ONE exception to this
// codebase's pattern of attaching "auth::AuthFilter" to every route via
// ADD_METHOD_TO -- there is no bearer token yet when a client is signing
// up or logging in, so neither route may go through that filter.
class AuthController : public drogon::HttpController<AuthController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AuthController::signup, "/v1/auth/signup", drogon::Post);
    ADD_METHOD_TO(AuthController::login, "/v1/auth/login", drogon::Post);
    METHOD_LIST_END

    void signup(const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    void login(const drogon::HttpRequestPtr &req,
               std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};

}  // namespace controllers

#pragma once

#include <drogon/HttpController.h>

namespace controllers
{
// Implements openapi.yaml's Verification paths: POST /verification,
// GET /verification/status. Both routes require auth::AuthFilter, which
// populates the "user_id" request attribute.
class VerificationController : public drogon::HttpController<VerificationController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(VerificationController::startVerification,
                  "/v1/verification",
                  drogon::Post,
                  "auth::AuthFilter");
    ADD_METHOD_TO(VerificationController::getStatus,
                  "/v1/verification/status",
                  drogon::Get,
                  "auth::AuthFilter");
    METHOD_LIST_END

    void startVerification(const drogon::HttpRequestPtr &req,
                           std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    void getStatus(const drogon::HttpRequestPtr &req,
                   std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};

}  // namespace controllers

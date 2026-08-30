#pragma once

#include <drogon/HttpController.h>

#include <functional>
#include <string>

namespace controllers
{
// Implements openapi.yaml's PinkyPromise paths (CUJ #4):
//   POST /v1/conversations/{id}/pinky-promise   -- A initiates
//   POST /v1/pinky-promises/{id}/confirm        -- B confirms
// Both require auth::AuthFilter, which populates the "user_id" request
// attribute (read via getUserId() in ControllerUtils.h).
class PinkyPromiseController : public drogon::HttpController<PinkyPromiseController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(PinkyPromiseController::initiate,
                  "/v1/conversations/{id}/pinky-promise",
                  drogon::Post,
                  "auth::AuthFilter");
    ADD_METHOD_TO(PinkyPromiseController::confirm,
                  "/v1/pinky-promises/{id}/confirm",
                  drogon::Post,
                  "auth::AuthFilter");
    METHOD_LIST_END

    void initiate(const drogon::HttpRequestPtr &req,
                  std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                  std::string id);
    void confirm(const drogon::HttpRequestPtr &req,
                 std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                 std::string id);
};

}  // namespace controllers

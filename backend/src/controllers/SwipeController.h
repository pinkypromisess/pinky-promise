#pragma once

#include <drogon/HttpController.h>

#include <functional>
#include <string>

namespace controllers
{
// Implements openapi.yaml's POST /proposals/{id}/swipe (CUJ #3's
// Heart/X write). Separate controller from ProposalController (Module B)
// -- Module C owns the swipe write path. The route requires
// auth::AuthFilter, which populates the "user_id" request attribute
// (read via getUserId() in ControllerUtils.h).
class SwipeController : public drogon::HttpController<SwipeController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(SwipeController::swipe,
                  "/v1/proposals/{id}/swipe",
                  drogon::Post,
                  "auth::AuthFilter");
    METHOD_LIST_END

    void swipe(const drogon::HttpRequestPtr &req,
               std::function<void(const drogon::HttpResponsePtr &)> &&callback,
               std::string id);
};

}  // namespace controllers

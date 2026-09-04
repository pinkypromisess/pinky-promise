#pragma once

#include <drogon/HttpController.h>

#include <functional>
#include <string>

namespace controllers
{
// Implements openapi.yaml's Block path (CUJ #10):
//   POST /v1/blocks -- caller blocks blocked_user_id
// Requires auth::AuthFilter, which populates the "user_id" request
// attribute (read via getUserId() in ControllerUtils.h).
class BlockController : public drogon::HttpController<BlockController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(BlockController::createBlock, "/v1/blocks", drogon::Post, "auth::AuthFilter");
    METHOD_LIST_END

    void createBlock(const drogon::HttpRequestPtr &req,
                      std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};

}  // namespace controllers

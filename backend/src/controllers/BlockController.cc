#include "BlockController.h"

#include "../AppContext.h"
#include "../services/BlockServiceErrors.h"
#include "ControllerUtils.h"

using namespace drogon;

namespace controllers
{
void BlockController::createBlock(const HttpRequestPtr &req,
                                   std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        callback(errorResponse(k400BadRequest, "INVALID_BODY", "Request body must be JSON."));
        return;
    }

    const auto userId = getUserId(req);
    const auto blockedUserId = jsonBody->get("blocked_user_id", "").asString();

    try
    {
        auto result = app_context::blockService().createBlock(userId, blockedUserId);
        callback(jsonResponse(result.block.toJson(),
                               result.wasCreated ? k201Created : k200OK));
    }
    catch (const services::BlockBadRequestException &e)
    {
        callback(errorResponse(k400BadRequest, e.code, e.what()));
    }
}

}  // namespace controllers

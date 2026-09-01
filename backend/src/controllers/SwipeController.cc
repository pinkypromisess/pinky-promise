#include "SwipeController.h"

#include "../AppContext.h"
#include "../services/ServiceErrors.h"
#include "../services/SwipeService.h"
#include "../services/SwipeServiceErrors.h"
#include "../validation/SwipeValidation.h"
#include "ControllerUtils.h"

using namespace drogon;

namespace controllers
{
void SwipeController::swipe(const HttpRequestPtr &req,
                            std::function<void(const HttpResponsePtr &)> &&callback,
                            std::string id)
{
    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        callback(errorResponse(k400BadRequest, "INVALID_BODY", "Request body must be JSON."));
        return;
    }

    const auto userId = getUserId(req);
    const auto action = jsonBody->get("action", "").asString();

    if (auto err = validation::validateSwipeAction(action))
    {
        callback(errorResponse(k400BadRequest, err->code, err->message));
        return;
    }

    try
    {
        auto swipe = app_context::swipeService().recordSwipe(userId, id, action);
        callback(jsonResponse(swipe.toJson(), k201Created));
    }
    catch (const services::NotFoundException &e)
    {
        callback(errorResponse(k404NotFound, "PROPOSAL_NOT_FOUND", e.what()));
    }
    catch (const services::SwipeBadRequestException &e)
    {
        callback(errorResponse(k400BadRequest, e.code, e.what()));
    }
    catch (const services::SwipeForbiddenException &e)
    {
        callback(errorResponse(k403Forbidden, e.code, e.what()));
    }
    catch (const services::SwipeConflictException &e)
    {
        callback(errorResponse(k409Conflict, e.code, e.what()));
    }
}

}  // namespace controllers

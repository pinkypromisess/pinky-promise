#include "PinkyPromiseController.h"

#include "../AppContext.h"
#include "../services/PinkyPromiseService.h"
#include "../services/PinkyPromiseServiceErrors.h"
#include "../services/ServiceErrors.h"
#include "ControllerUtils.h"

using namespace drogon;

namespace controllers
{
void PinkyPromiseController::initiate(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback,
    std::string id)
{
    const auto userId = getUserId(req);
    try
    {
        auto pp = app_context::pinkyPromiseService().initiate(id, userId);
        callback(jsonResponse(pp.toJson(), k201Created));
    }
    catch (const services::NotFoundException &e)
    {
        callback(errorResponse(k404NotFound, "CONVERSATION_NOT_FOUND", e.what()));
    }
    catch (const services::PinkyPromiseForbiddenException &e)
    {
        callback(errorResponse(k403Forbidden, e.code, e.what()));
    }
    catch (const services::PinkyPromiseConflictException &e)
    {
        callback(errorResponse(k409Conflict, e.code, e.what()));
    }
}

void PinkyPromiseController::confirm(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback,
    std::string id)
{
    const auto userId = getUserId(req);
    try
    {
        auto pp = app_context::pinkyPromiseService().confirm(id, userId);
        callback(jsonResponse(pp.toJson(), k200OK));
    }
    catch (const services::NotFoundException &e)
    {
        callback(errorResponse(k404NotFound, "PINKY_PROMISE_NOT_FOUND", e.what()));
    }
    catch (const services::PinkyPromiseForbiddenException &e)
    {
        callback(errorResponse(k403Forbidden, e.code, e.what()));
    }
    catch (const services::PinkyPromiseConflictException &e)
    {
        callback(errorResponse(k409Conflict, e.code, e.what()));
    }
}

}  // namespace controllers

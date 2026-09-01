#include "ConversationController.h"

#include "../AppContext.h"
#include "../services/ConversationService.h"
#include "../services/ConversationServiceErrors.h"
#include "../services/ServiceErrors.h"
#include "../validation/MessageValidation.h"
#include "ControllerUtils.h"

using namespace drogon;

namespace controllers
{
void ConversationController::listConversations(
    const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback)
{
    const auto userId = getUserId(req);
    auto items = app_context::conversationService().listForUser(userId);

    Json::Value conversationsJson(Json::arrayValue);
    for (const auto &item : items)
    {
        conversationsJson.append(item.toJson());
    }
    Json::Value body;
    body["conversations"] = conversationsJson;
    callback(jsonResponse(body, k200OK));
}

void ConversationController::getConversation(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback,
    std::string id)
{
    const auto userId = getUserId(req);
    try
    {
        auto view = app_context::conversationService().getForUser(id, userId);
        callback(jsonResponse(view.toJson(), k200OK));
    }
    catch (const services::NotFoundException &e)
    {
        callback(errorResponse(k404NotFound, "CONVERSATION_NOT_FOUND", e.what()));
    }
    catch (const services::ConversationForbiddenException &e)
    {
        callback(errorResponse(k403Forbidden, "NOT_A_PARTICIPANT", e.what()));
    }
}

void ConversationController::postMessage(
    const HttpRequestPtr &req,
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
    const auto type = jsonBody->get("type", "").asString();
    const auto content = jsonBody->get("content", "").asString();

    if (auto err = validation::validateMessage(type, content))
    {
        callback(errorResponse(k400BadRequest, err->code, err->message));
        return;
    }

    try
    {
        auto msg = app_context::conversationService().postMessage(id, userId, type, content);
        callback(jsonResponse(msg.toJson(), k201Created));
    }
    catch (const services::NotFoundException &e)
    {
        callback(errorResponse(k404NotFound, "CONVERSATION_NOT_FOUND", e.what()));
    }
    catch (const services::ConversationForbiddenException &e)
    {
        callback(errorResponse(k403Forbidden, "NOT_A_PARTICIPANT", e.what()));
    }
    catch (const services::ConversationExpiredException &e)
    {
        callback(errorResponse(k409Conflict, "CONVERSATION_EXPIRED", e.what()));
    }
}

}  // namespace controllers

#pragma once

#include <drogon/HttpController.h>

#include <functional>
#include <string>

namespace controllers
{
// Implements openapi.yaml's Conversation paths: GET /v1/conversations,
// GET /v1/conversations/{id}, POST /v1/conversations/{id}/messages
// (CUJ #4). All routes require auth::AuthFilter, which populates the
// "user_id" request attribute (read via getUserId() in ControllerUtils.h).
class ConversationController : public drogon::HttpController<ConversationController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ConversationController::listConversations,
                  "/v1/conversations",
                  drogon::Get,
                  "auth::AuthFilter");
    ADD_METHOD_TO(ConversationController::getConversation,
                  "/v1/conversations/{id}",
                  drogon::Get,
                  "auth::AuthFilter");
    ADD_METHOD_TO(ConversationController::postMessage,
                  "/v1/conversations/{id}/messages",
                  drogon::Post,
                  "auth::AuthFilter");
    METHOD_LIST_END

    void listConversations(const drogon::HttpRequestPtr &req,
                           std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    void getConversation(const drogon::HttpRequestPtr &req,
                         std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                         std::string id);
    void postMessage(const drogon::HttpRequestPtr &req,
                     std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                     std::string id);
};

}  // namespace controllers

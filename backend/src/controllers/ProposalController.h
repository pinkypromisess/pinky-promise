#pragma once

#include <drogon/HttpController.h>

#include <string>

namespace controllers
{
// Implements openapi.yaml's Proposal paths: POST /proposals,
// GET /proposals/feed, DELETE /proposals/{id}. All routes require
// auth::AuthFilter, which populates the "user_id" request attribute.
class ProposalController : public drogon::HttpController<ProposalController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ProposalController::createProposal,
                  "/v1/proposals",
                  drogon::Post,
                  "auth::AuthFilter");
    ADD_METHOD_TO(ProposalController::getFeed,
                  "/v1/proposals/feed",
                  drogon::Get,
                  "auth::AuthFilter");
    ADD_METHOD_TO(ProposalController::deleteProposal,
                  "/v1/proposals/{id}",
                  drogon::Delete,
                  "auth::AuthFilter");
    METHOD_LIST_END

    void createProposal(const drogon::HttpRequestPtr &req,
                        std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    void getFeed(const drogon::HttpRequestPtr &req,
                std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    void deleteProposal(const drogon::HttpRequestPtr &req,
                        std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        std::string id);
};

}  // namespace controllers

#pragma once

#include <drogon/HttpController.h>

#include <functional>
#include <string>

namespace controllers
{
// Implements openapi.yaml's Report path (CUJ #10):
//   POST /v1/reports -- caller reports a profile/proposal/conversation
// Requires auth::AuthFilter, which populates the "user_id" request
// attribute (read via getUserId() in ControllerUtils.h). On success this
// also auto-blocks the resolved reported user, by calling the existing
// BlockService (see the .cc) -- not duplicating E.1's cascade SQL here.
class ReportController : public drogon::HttpController<ReportController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ReportController::createReport, "/v1/reports", drogon::Post, "auth::AuthFilter");
    METHOD_LIST_END

    void createReport(const drogon::HttpRequestPtr &req,
                       std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};

}  // namespace controllers

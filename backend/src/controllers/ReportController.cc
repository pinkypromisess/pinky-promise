#include "ReportController.h"

#include "../AppContext.h"
#include "../services/ReportServiceErrors.h"
#include "../services/ServiceErrors.h"
#include "../validation/ReportValidation.h"
#include "ControllerUtils.h"

using namespace drogon;

namespace controllers
{
namespace
{
validation::ReportCreateInput parseCreateInput(const Json::Value &body)
{
    validation::ReportCreateInput input;
    input.targetType = body.get("target_type", "").asString();
    input.targetId = body.get("target_id", "").asString();
    input.reason = body.get("reason", "").asString();
    input.detailsText = body.get("details_text", "").asString();
    return input;
}

}  // namespace

void ReportController::createReport(const HttpRequestPtr &req,
                                     std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        callback(errorResponse(k400BadRequest, "INVALID_BODY", "Request body must be JSON."));
        return;
    }

    const auto userId = getUserId(req);
    const auto input = parseCreateInput(*jsonBody);

    auto errors = validation::validateReportCreate(input);
    if (!errors.empty())
    {
        callback(validationErrorResponse(errors));
        return;
    }

    try
    {
        const auto reportedUserId = app_context::reportService().resolveReportedUserId(
            userId, input.targetType, input.targetId);

        auto report = app_context::reportService().createReportRow(
            userId, input.targetType, input.targetId, input.reason, input.detailsText);

        // Reuses E.1's BlockService::createBlock exactly as-is -- no
        // cascade SQL duplicated here. Composed at the controller layer
        // rather than service-to-service: no service in this codebase
        // calls another service directly today (each only owns its own
        // DbClientPtr), so this follows the existing composition style of
        // wiring cross-service calls through app_context from the
        // controller. This is intentionally two separate statements (the
        // report insert above, then this call, which is atomic on its
        // own) rather than one combined transaction -- see the E.2
        // report for the flagged non-atomicity gap. reportedUserId always
        // names a real user (it's read off users.id / proposals.
        // creator_user_id / a conversation's participant columns, all FK
        // to users), so BlockService's own USER_NOT_FOUND/CANNOT_
        // BLOCK_SELF paths cannot fire here -- CANNOT_REPORT_SELF above
        // already excludes the self case.
        app_context::blockService().createBlock(userId, reportedUserId);

        callback(jsonResponse(report.toJson(), k201Created));
    }
    catch (const services::NotFoundException &e)
    {
        callback(errorResponse(k404NotFound, "TARGET_NOT_FOUND", e.what()));
    }
    catch (const services::ReportBadRequestException &e)
    {
        callback(errorResponse(k400BadRequest, e.code, e.what()));
    }
}

}  // namespace controllers

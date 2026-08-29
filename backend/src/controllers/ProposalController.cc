#include "ProposalController.h"

#include <optional>
#include <stdexcept>

#include "../AppContext.h"
#include "../services/ProposalServiceErrors.h"
#include "../services/ServiceErrors.h"
#include "../validation/ProposalValidation.h"
#include "ControllerUtils.h"

using namespace drogon;

namespace controllers
{
namespace
{
validation::ProposalCreateInput parseCreateInput(const Json::Value &body)
{
    validation::ProposalCreateInput input;
    input.activityText = body.get("activity_text", "").asString();
    input.eventTime = body.get("event_time", "").asString();
    input.paymentType = body.get("payment_type", "").asString();
    input.lookingForText = body.get("looking_for_text", "").asString();

    if (body.isMember("location") && body["location"].isObject())
    {
        const auto &location = body["location"];
        input.location.lat = location.get("lat", 0.0).asDouble();
        input.location.lng = location.get("lng", 0.0).asDouble();
        input.location.address = location.get("address", "").asString();
    }

    if (body.isMember("revealed_fields") && body["revealed_fields"].isArray())
    {
        for (const auto &field : body["revealed_fields"])
        {
            input.revealedFields.push_back(field.asString());
        }
    }

    return input;
}

}  // namespace

void ProposalController::createProposal(const HttpRequestPtr &req,
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

    try
    {
        auto proposal = app_context::proposalService().createProposal(userId, input);
        callback(jsonResponse(proposal.toJson(), k201Created));
    }
    catch (const services::ValidationFailedException &e)
    {
        callback(validationErrorResponse(e.errors));
    }
    catch (const services::PostingNotAllowedException &e)
    {
        callback(errorResponse(k403Forbidden, e.code, e.what()));
    }
}

void ProposalController::getFeed(const HttpRequestPtr &req,
                                 std::function<void(const HttpResponsePtr &)> &&callback)
{
    const auto userId = getUserId(req);

    std::optional<double> lat;
    std::optional<double> lng;
    const auto &params = req->getParameters();
    auto latIt = params.find("lat");
    auto lngIt = params.find("lng");
    if (latIt != params.end() && lngIt != params.end())
    {
        try
        {
            lat = std::stod(latIt->second);
            lng = std::stod(lngIt->second);
        }
        catch (const std::exception &)
        {
            callback(errorResponse(
                k400BadRequest, "INVALID_QUERY_PARAM", "lat/lng must both be numbers."));
            return;
        }
    }

    auto items = app_context::proposalService().getFeed(userId, lat, lng);

    Json::Value proposalsJson(Json::arrayValue);
    for (const auto &item : items)
    {
        proposalsJson.append(item.toJson());
    }
    Json::Value body;
    body["proposals"] = proposalsJson;
    callback(jsonResponse(body, k200OK));
}

void ProposalController::deleteProposal(const HttpRequestPtr &req,
                                        std::function<void(const HttpResponsePtr &)> &&callback,
                                        std::string id)
{
    const auto userId = getUserId(req);

    try
    {
        app_context::proposalService().deleteProposal(userId, id);
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k204NoContent);
        callback(resp);
    }
    catch (const services::NotFoundException &e)
    {
        callback(errorResponse(k404NotFound, "PROPOSAL_NOT_FOUND", e.what()));
    }
}

}  // namespace controllers

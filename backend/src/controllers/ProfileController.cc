#include "ProfileController.h"

#include "../AppContext.h"
#include "../services/ServiceErrors.h"
#include "../validation/SwipeValidation.h"  // reused for validation::isLikelyUuid, same as
                                             // ConversationService/PinkyPromiseService already do
#include "ControllerUtils.h"

using namespace drogon;

namespace controllers
{
namespace
{
validation::ProfileUpsertInput parseUpsertInput(const Json::Value &body)
{
    validation::ProfileUpsertInput input;
    input.name = body.get("name", "").asString();
    input.sex = body.get("sex", "").asString();
    input.age = body.get("age", 0).asInt();
    input.needToKnowText = body.get("need_to_know_text", "").asString();

    if (body.isMember("photo_urls") && body["photo_urls"].isArray())
    {
        for (const auto &url : body["photo_urls"])
        {
            input.photoUrls.push_back(url.asString());
        }
    }

    if (body.isMember("occupation") && !body["occupation"].isNull())
    {
        input.occupation = body["occupation"].asString();
    }
    if (body.isMember("relationship_status") && !body["relationship_status"].isNull())
    {
        input.relationshipStatus = body["relationship_status"].asString();
    }

    return input;
}

}  // namespace

void ProfileController::putProfile(const HttpRequestPtr &req,
                                    std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        callback(errorResponse(k400BadRequest, "INVALID_BODY", "Request body must be JSON."));
        return;
    }

    const auto userId = getUserId(req);
    const auto input = parseUpsertInput(*jsonBody);

    try
    {
        auto profile = app_context::profileService().upsertProfile(userId, input);
        callback(jsonResponse(profile.toJson(), k200OK));
    }
    catch (const services::ValidationFailedException &e)
    {
        callback(validationErrorResponse(e.errors));
    }
}

void ProfileController::patchPhotos(const HttpRequestPtr &req,
                                     std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        callback(errorResponse(k400BadRequest, "INVALID_BODY", "Request body must be JSON."));
        return;
    }

    std::vector<std::string> addUrls;
    if (jsonBody->isMember("add") && (*jsonBody)["add"].isArray())
    {
        for (const auto &url : (*jsonBody)["add"])
        {
            addUrls.push_back(url.asString());
        }
    }

    std::vector<std::string> removeIds;
    if (jsonBody->isMember("remove") && (*jsonBody)["remove"].isArray())
    {
        for (const auto &id : (*jsonBody)["remove"])
        {
            removeIds.push_back(id.asString());
        }
    }

    const auto userId = getUserId(req);

    try
    {
        auto profile = app_context::profileService().patchPhotos(userId, addUrls, removeIds);
        callback(jsonResponse(profile.toJson(), k200OK));
    }
    catch (const services::ValidationFailedException &e)
    {
        callback(validationErrorResponse(e.errors));
    }
    catch (const services::NotFoundException &e)
    {
        callback(errorResponse(k404NotFound, "PROFILE_NOT_FOUND", e.what()));
    }
}

void ProfileController::getMyProfile(const HttpRequestPtr &req,
                                      std::function<void(const HttpResponsePtr &)> &&callback)
{
    const auto userId = getUserId(req);

    try
    {
        auto profile = app_context::profileService().getProfile(userId);
        callback(jsonResponse(profile.toJson(), k200OK));
    }
    catch (const services::NotFoundException &)
    {
        callback(errorResponse(
            k404NotFound, "PROFILE_NOT_FOUND", "You haven't created a profile yet."));
    }
}

void ProfileController::getUserProfile(const HttpRequestPtr &req,
                                        std::function<void(const HttpResponsePtr &)> &&callback,
                                        std::string userId)
{
    (void)req;  // auth::AuthFilter has already required a valid caller; the
                // caller's own id isn't otherwise needed for a bare view.
    if (!validation::isLikelyUuid(userId))
    {
        callback(errorResponse(k404NotFound, "PROFILE_NOT_FOUND", "No such profile."));
        return;
    }
    try
    {
        auto profile = app_context::profileService().getProfile(userId);

        // Bare view only — fields 1-5, per CUJ #4. Deliberately built by
        // hand rather than via Profile::toJson() so a future field added to
        // the full profile JSON doesn't silently leak into this endpoint.
        Json::Value j;
        j["user_id"] = profile.userId;
        j["name"] = profile.name;
        j["sex"] = profile.sex;
        j["age"] = profile.age;
        j["need_to_know_text"] = profile.needToKnowText;
        Json::Value photosJson(Json::arrayValue);
        for (const auto &photo : profile.photos)
        {
            photosJson.append(photo.toJson());
        }
        j["photos"] = photosJson;

        callback(jsonResponse(j, k200OK));
    }
    catch (const services::NotFoundException &)
    {
        callback(errorResponse(k404NotFound, "PROFILE_NOT_FOUND", "No such profile."));
    }
}

void ProfileController::createPhotoUploadUrl(
    const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        callback(errorResponse(k400BadRequest, "INVALID_BODY", "Request body must be JSON."));
        return;
    }

    const auto userId = getUserId(req);
    const auto contentType = jsonBody->get("content_type", "").asString();

    try
    {
        auto upload = app_context::photoUploadService().createUploadUrl(userId, contentType);
        callback(jsonResponse(upload.toJson(), k200OK));
    }
    catch (const services::ValidationFailedException &e)
    {
        callback(validationErrorResponse(e.errors));
    }
}

}  // namespace controllers

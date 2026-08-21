#pragma once

#include <drogon/HttpController.h>

namespace controllers
{
// Implements openapi.yaml's Profile paths: PUT /profile,
// PATCH /profile/photos, GET /profile/me. All routes require
// auth::AuthFilter, which populates the "user_id" request attribute.
class ProfileController : public drogon::HttpController<ProfileController>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ProfileController::putProfile, "/v1/profile", drogon::Put, "auth::AuthFilter");
    ADD_METHOD_TO(ProfileController::patchPhotos,
                  "/v1/profile/photos",
                  drogon::Patch,
                  "auth::AuthFilter");
    ADD_METHOD_TO(ProfileController::getMyProfile,
                  "/v1/profile/me",
                  drogon::Get,
                  "auth::AuthFilter");
    METHOD_LIST_END

    void putProfile(const drogon::HttpRequestPtr &req,
                    std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    void patchPhotos(const drogon::HttpRequestPtr &req,
                     std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    void getMyProfile(const drogon::HttpRequestPtr &req,
                      std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};

}  // namespace controllers

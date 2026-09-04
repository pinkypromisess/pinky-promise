#pragma once

#include <drogon/HttpController.h>

namespace controllers
{
// Implements openapi.yaml's Profile paths: PUT /profile,
// PATCH /profile/photos, GET /profile/me, GET /profile/{user_id},
// POST /profile/photos/upload-url. All routes require auth::AuthFilter,
// which populates the "user_id" request attribute.
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
    ADD_METHOD_TO(ProfileController::createPhotoUploadUrl,
                  "/v1/profile/photos/upload-url",
                  drogon::Post,
                  "auth::AuthFilter");
    ADD_METHOD_TO(ProfileController::getUserProfile,
                  "/v1/profile/{user_id}",
                  drogon::Get,
                  "auth::AuthFilter");
    METHOD_LIST_END

    void putProfile(const drogon::HttpRequestPtr &req,
                    std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    void patchPhotos(const drogon::HttpRequestPtr &req,
                     std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    void getMyProfile(const drogon::HttpRequestPtr &req,
                      std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    void createPhotoUploadUrl(const drogon::HttpRequestPtr &req,
                              std::function<void(const drogon::HttpResponsePtr &)> &&callback);
    // Bare profile view of another user (CUJ #4: "A can view B's profile —
    // fields 1,2,3,4,5 only"). Returns only name/sex/age/need_to_know_text/
    // photos — never occupation, relationship_status, verified, or
    // created_at. No per-Proposal reveal applies here (there is no
    // Proposal context), and no participant/conversation gating: this is a
    // strict subset of what's already shown to every browsing user on a
    // Proposal card (see ProposalService::loadCardProfile), so exposing it
    // by user_id alone introduces no new information disclosure.
    void getUserProfile(const drogon::HttpRequestPtr &req,
                        std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                        std::string userId);
};

}  // namespace controllers

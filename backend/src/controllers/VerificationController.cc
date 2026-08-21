#include "VerificationController.h"

#include "../AppContext.h"
#include "../services/ServiceErrors.h"
#include "ControllerUtils.h"

using namespace drogon;

namespace controllers
{
void VerificationController::startVerification(
    const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback)
{
    const auto userId = getUserId(req);

    try
    {
        auto verificationAttempt = app_context::verificationService().startVerification(userId);
        callback(jsonResponse(verificationAttempt.toJson(), k201Created));
    }
    catch (const services::NotFoundException &)
    {
        callback(errorResponse(
            k400BadRequest, "PROFILE_NOT_FOUND", "You must create a profile before verifying."));
    }
    catch (const services::ValidationFailedException &e)
    {
        callback(validationErrorResponse(e.errors));
    }
}

void VerificationController::getStatus(const HttpRequestPtr &req,
                                        std::function<void(const HttpResponsePtr &)> &&callback)
{
    const auto userId = getUserId(req);

    try
    {
        auto verificationAttempt = app_context::verificationService().getStatus(userId);
        callback(jsonResponse(verificationAttempt.toJson(), k200OK));
    }
    catch (const services::NotFoundException &e)
    {
        callback(errorResponse(k404NotFound, "VERIFICATION_NOT_FOUND", e.what()));
    }
}

}  // namespace controllers

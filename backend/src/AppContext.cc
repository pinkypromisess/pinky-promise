#include "AppContext.h"

#include <stdexcept>

namespace app_context
{
namespace
{
std::unique_ptr<services::ProfileService> profileServiceInstance;
std::unique_ptr<services::VerificationService> verificationServiceInstance;
std::unique_ptr<services::ProposalService> proposalServiceInstance;
std::unique_ptr<services::PhotoUploadService> photoUploadServiceInstance;

}  // namespace

void init(drogon::orm::DbClientPtr db,
          std::shared_ptr<verification::FaceVerificationProvider> faceVerificationProvider,
          std::shared_ptr<storage::GcsUploadUrlProvider> photoUploadProvider)
{
    profileServiceInstance = std::make_unique<services::ProfileService>(db);
    proposalServiceInstance = std::make_unique<services::ProposalService>(db);
    verificationServiceInstance = std::make_unique<services::VerificationService>(
        std::move(db), std::move(faceVerificationProvider));
    photoUploadServiceInstance =
        std::make_unique<services::PhotoUploadService>(std::move(photoUploadProvider));
}

services::ProfileService &profileService()
{
    if (!profileServiceInstance)
    {
        throw std::runtime_error("app_context::init() was not called before profileService()");
    }
    return *profileServiceInstance;
}

services::VerificationService &verificationService()
{
    if (!verificationServiceInstance)
    {
        throw std::runtime_error(
            "app_context::init() was not called before verificationService()");
    }
    return *verificationServiceInstance;
}

services::ProposalService &proposalService()
{
    if (!proposalServiceInstance)
    {
        throw std::runtime_error("app_context::init() was not called before proposalService()");
    }
    return *proposalServiceInstance;
}

services::PhotoUploadService &photoUploadService()
{
    if (!photoUploadServiceInstance)
    {
        throw std::runtime_error(
            "app_context::init() was not called before photoUploadService()");
    }
    return *photoUploadServiceInstance;
}

}  // namespace app_context

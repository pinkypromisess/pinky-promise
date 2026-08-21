#include "AppContext.h"

#include <stdexcept>

namespace app_context
{
namespace
{
std::unique_ptr<services::ProfileService> profileServiceInstance;
std::unique_ptr<services::VerificationService> verificationServiceInstance;

}  // namespace

void init(drogon::orm::DbClientPtr db,
          std::shared_ptr<verification::FaceVerificationProvider> provider)
{
    profileServiceInstance = std::make_unique<services::ProfileService>(db);
    verificationServiceInstance =
        std::make_unique<services::VerificationService>(std::move(db), std::move(provider));
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

}  // namespace app_context

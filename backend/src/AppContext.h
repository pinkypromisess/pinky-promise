#pragma once

#include <drogon/orm/DbClient.h>

#include <memory>

#include "services/ProfileService.h"
#include "services/VerificationService.h"
#include "verification/FaceVerificationProvider.h"

// Tiny hand-rolled service locator. Controllers are constructed by
// Drogon's own machinery, so they can't take constructor-injected
// dependencies the normal way — this gives them a single place to reach
// the (few) shared services instead of each owning its own DB client.
namespace app_context
{
void init(drogon::orm::DbClientPtr db,
          std::shared_ptr<verification::FaceVerificationProvider> provider);

services::ProfileService &profileService();
services::VerificationService &verificationService();

}  // namespace app_context

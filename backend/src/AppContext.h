#pragma once

#include <drogon/orm/DbClient.h>

#include <memory>

#include "services/PhotoUploadService.h"
#include "services/ProfileService.h"
#include "services/ProposalService.h"
#include "services/VerificationService.h"
#include "storage/GcsUploadUrlProvider.h"
#include "verification/FaceVerificationProvider.h"

// Tiny hand-rolled service locator. Controllers are constructed by
// Drogon's own machinery, so they can't take constructor-injected
// dependencies the normal way — this gives them a single place to reach
// the (few) shared services instead of each owning its own DB client.
namespace app_context
{
void init(drogon::orm::DbClientPtr db,
          std::shared_ptr<verification::FaceVerificationProvider> faceVerificationProvider,
          std::shared_ptr<storage::GcsUploadUrlProvider> photoUploadProvider);

services::ProfileService &profileService();
services::VerificationService &verificationService();
services::ProposalService &proposalService();
services::PhotoUploadService &photoUploadService();

}  // namespace app_context

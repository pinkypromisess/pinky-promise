#pragma once

#include <drogon/orm/DbClient.h>

#include <memory>

#include "auth/SocialTokenVerifier.h"
#include "notifications/ReminderProvider.h"
#include "services/AuthService.h"
#include "services/BlockService.h"
#include "services/ConversationService.h"
#include "services/PhotoUploadService.h"
#include "services/PinkyPromiseService.h"
#include "services/ProfileService.h"
#include "services/ProposalService.h"
#include "services/ReminderService.h"
#include "services/ReportService.h"
#include "services/SwipeService.h"
#include "services/VerificationService.h"
#include "storage/GcsUploadUrlProvider.h"
#include "verification/FaceVerificationProvider.h"

// Tiny hand-rolled service locator. Controllers are constructed by
// Drogon's own machinery, so they can't take constructor-injected
// dependencies the normal way — this gives them a single place to reach
// the (few) shared services instead of each owning its own DB client.
namespace app_context
{
// `googleSocialVerifier`/`appleSocialVerifier` added for Module F.2 --
// AuthService needs one auth::SocialTokenVerifier per provider, injected
// the same way faceVerificationProvider/photoUploadProvider/
// reminderProvider already are (real implementations from main.cc, stubs
// from TestHttpServer.cc). This is the one Module F.2 signature change
// flagged in that module's brief; every other init() parameter is
// unchanged.
void init(drogon::orm::DbClientPtr db,
          std::shared_ptr<verification::FaceVerificationProvider> faceVerificationProvider,
          std::shared_ptr<storage::GcsUploadUrlProvider> photoUploadProvider,
          std::shared_ptr<notifications::ReminderProvider> reminderProvider,
          std::shared_ptr<auth::SocialTokenVerifier> googleSocialVerifier,
          std::shared_ptr<auth::SocialTokenVerifier> appleSocialVerifier);

services::ProfileService &profileService();
services::VerificationService &verificationService();
services::ProposalService &proposalService();
services::PhotoUploadService &photoUploadService();
services::SwipeService &swipeService();
services::ConversationService &conversationService();
services::PinkyPromiseService &pinkyPromiseService();
services::ReminderService &reminderService();
services::BlockService &blockService();
services::ReportService &reportService();
services::AuthService &authService();

}  // namespace app_context

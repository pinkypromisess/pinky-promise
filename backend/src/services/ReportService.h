#pragma once

#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include <string>

#include "ReportServiceErrors.h"
#include "ServiceErrors.h"

namespace services
{
struct Report
{
    std::string id;
    std::string reporterUserId;
    std::string targetType;
    std::string targetId;
    std::string reason;
    std::string detailsText;
    std::string status;
    std::string createdAt;

    Json::Value toJson() const;
};

// Owns Report persistence and target-resolution (CUJ #10). Deliberately
// does NOT own the auto-block side effect -- per the E.2 brief, that
// reuses BlockService::createBlock as-is (no cascade SQL duplicated
// here), composed from the controller layer the same way every other
// cross-service call in this codebase is (via app_context), since no
// service here calls another service directly today. Every DB call is
// synchronous (drogon::orm::DbClient::execSqlSync), matching
// BlockService / ProposalService / SwipeService.
class ReportService
{
  public:
    explicit ReportService(drogon::orm::DbClientPtr db);

    // Resolves (target_type, target_id) to the user being reported (the
    // one the caller should auto-block):
    //   "profile"      -> target_id IS the reported user's id directly.
    //   "proposal"     -> proposals.creator_user_id for target_id.
    //   "conversation" -> whichever of proposer_user_id/interested_user_id
    //                     on target_id is NOT callerUserId.
    //
    // Throws:
    //  - NotFoundException (404, mapped to TARGET_NOT_FOUND by the
    //    controller) if target_id doesn't name a real row of the given
    //    target_type.
    //  - ReportBadRequestException (400, NOT_A_PARTICIPANT) if
    //    target_type == "conversation" and callerUserId is neither
    //    participant.
    //  - ReportBadRequestException (400, CANNOT_REPORT_SELF) if the
    //    resolved reported-user-id equals callerUserId -- checked here,
    //    before any report row is inserted or BlockService is called.
    //
    // `targetType`/`targetId` are assumed already field-validated
    // (validation::validateReportCreate) -- targetType is one of the
    // three known values and targetId is uuid-shaped.
    std::string resolveReportedUserId(const std::string &callerUserId,
                                       const std::string &targetType,
                                       const std::string &targetId);

    // Inserts the report row (reporter_user_id = callerUserId, `status`
    // defaults to 'open'). Pure insert -- no cascade, no auto-block; the
    // caller is responsible for calling BlockService::createBlock
    // separately afterward. Reports are NOT deduplicated the way Blocks
    // are: reporting the same target twice creates two rows.
    Report createReportRow(const std::string &callerUserId,
                            const std::string &targetType,
                            const std::string &targetId,
                            const std::string &reason,
                            const std::string &detailsText);

  private:
    drogon::orm::DbClientPtr db_;
};

}  // namespace services

#pragma once

#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include <string>

#include "ServiceErrors.h"
#include "SwipeServiceErrors.h"

namespace services
{
// The "10 interested per day" rule (CUJ #3 / Global rules), interpreted as
// a rolling 24h window per the C.1 brief's manager scoping decision -- NOT
// calendar-day. If calendar-day semantics turn out to be required, this
// constant and the interval in recordSwipe() are the two places to change,
// but that's a spec change to flag, not a silent switch.
constexpr int kInterestedRollingDayCap = 10;

struct Swipe
{
    std::string id;
    std::string proposalId;
    std::string action;
    std::string createdAt;

    Json::Value toJson() const;
};

// Owns Swipe persistence and the swipe-time gating (verified check +
// rolling-24h interested cap). Every DB call is synchronous
// (drogon::orm::DbClient::execSqlSync), matching ProposalService /
// ProfileService.
class SwipeService
{
  public:
    explicit SwipeService(drogon::orm::DbClientPtr db);

    // Records a swipe by `swiperUserId` on proposal `proposalId`.
    //
    // Throws:
    //  - NotFoundException (404) if the proposal id is malformed, doesn't
    //    exist, or isn't 'active'.
    //  - SwipeBadRequestException (400, CANNOT_SWIPE_OWN_PROPOSAL) if the
    //    caller is the proposal's creator.
    //  - SwipeForbiddenException (403, PROFILE_NOT_VERIFIED) on an
    //    'interested' swipe when the caller's profile isn't verified.
    //  - SwipeForbiddenException (403, INTERESTED_DAILY_CAP_REACHED) on an
    //    'interested' swipe when the caller already has
    //    kInterestedRollingDayCap 'interested' rows in the last 24h.
    //  - SwipeConflictException (409, ALREADY_SWIPED) if this
    //    (proposal, user) pair already has a swipe row.
    //
    // `action` is assumed already validated to be "interested" | "pass".
    Swipe recordSwipe(const std::string &swiperUserId,
                       const std::string &proposalId,
                       const std::string &action);

  private:
    drogon::orm::DbClientPtr db_;
};

}  // namespace services

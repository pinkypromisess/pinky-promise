#include "SwipeService.h"

#include <drogon/orm/Exception.h>

#include <cstdint>
#include <exception>
#include <string>

#include "../validation/SwipeValidation.h"

namespace services
{
namespace
{
// True if a drogon ORM exception is a Postgres unique-constraint
// violation (SQLSTATE 23505). Checked via the generic SqlError base +
// sqlState() rather than by catching drogon::orm::UniqueViolation
// directly, so this holds whether or not the backend classified it into
// the fine-grained subclass.
bool isUniqueViolation(const drogon::orm::DrogonDbException &e)
{
    const auto *sqlError = dynamic_cast<const drogon::orm::SqlError *>(&e.base());
    return sqlError != nullptr && sqlError->sqlState() == "23505";
}

}  // namespace

Json::Value Swipe::toJson() const
{
    Json::Value j;
    j["id"] = id;
    j["proposal_id"] = proposalId;
    j["action"] = action;
    j["created_at"] = createdAt;
    return j;
}

SwipeService::SwipeService(drogon::orm::DbClientPtr db) : db_(std::move(db))
{
}

Swipe SwipeService::recordSwipe(const std::string &swiperUserId,
                                 const std::string &proposalId,
                                 const std::string &action)
{
    // A malformed id can't name a real proposal -- treat it exactly like
    // "not found" rather than letting Postgres reject the uuid bind param.
    if (!validation::isLikelyUuid(proposalId))
    {
        throw NotFoundException("No proposal " + proposalId + ".");
    }

    // 1. Proposal must exist and be active (a cancelled / expired /
    // pinky_promised proposal is not swipeable; same 404 either way so we
    // don't leak which case it is).
    auto proposalRows = db_->execSqlSync(
        "SELECT creator_user_id, status FROM proposals WHERE id = $1", proposalId);
    if (proposalRows.empty())
    {
        throw NotFoundException("No proposal " + proposalId + ".");
    }
    const auto creatorUserId = proposalRows[0]["creator_user_id"].as<std::string>();
    const auto status = proposalRows[0]["status"].as<std::string>();
    if (status != "active")
    {
        throw NotFoundException("Proposal " + proposalId + " is not active.");
    }

    // 2. Can't swipe on your own proposal (chose 400 over 403 -- it's a
    // malformed request, not a permissions problem).
    if (creatorUserId == swiperUserId)
    {
        throw SwipeBadRequestException("CANNOT_SWIPE_OWN_PROPOSAL",
                                        "You cannot swipe on your own Proposal.");
    }

    // 3. 'interested'-only gates. 'pass' skips all of this (allowed while
    // unverified, uncapped).
    if (action == "interested")
    {
        auto profileRows =
            db_->execSqlSync("SELECT verified FROM profiles WHERE user_id = $1", swiperUserId);
        const bool verified = !profileRows.empty() && profileRows[0]["verified"].as<bool>();
        if (!verified)
        {
            throw SwipeForbiddenException(
                "PROFILE_NOT_VERIFIED",
                "Verification is required to express interest in a Proposal.");
        }

        // Rolling 24h window (manager scoping decision -- see the note on
        // kInterestedRollingDayCap). The interval is a constant literal;
        // only swiperUserId is a bind param.
        auto countRows = db_->execSqlSync(
            "SELECT count(*) AS c FROM swipes "
            "WHERE swiper_user_id = $1 AND action = 'interested' "
            "AND created_at > now() - interval '24 hours'",
            swiperUserId);
        const auto interestedInLastDay = countRows[0]["c"].as<int64_t>();
        if (interestedInLastDay >= static_cast<int64_t>(kInterestedRollingDayCap))
        {
            throw SwipeForbiddenException(
                "INTERESTED_DAILY_CAP_REACHED",
                "You can express interest in at most " +
                    std::to_string(kInterestedRollingDayCap) + " Proposals per 24 hours.");
        }
    }

    // 4. Pre-check the one-swipe-per-(proposal,user) rule for a clean 409
    // in the common case...
    auto existing = db_->execSqlSync(
        "SELECT 1 FROM swipes WHERE proposal_id = $1 AND swiper_user_id = $2",
        proposalId,
        swiperUserId);
    if (!existing.empty())
    {
        throw SwipeConflictException("ALREADY_SWIPED",
                                      "You have already swiped on this Proposal.");
    }

    // ...and still rely on the UNIQUE constraint as the real guard: the
    // pre-check above races with a concurrent request, so map a 23505 from
    // the INSERT to the same 409 rather than surfacing a 500.
    try
    {
        auto inserted = db_->execSqlSync(
            "INSERT INTO swipes (proposal_id, swiper_user_id, action) VALUES ($1, $2, $3) "
            "RETURNING id, proposal_id, action, created_at",
            proposalId,
            swiperUserId,
            action);

        const auto &row = inserted[0];
        Swipe swipe;
        swipe.id = row["id"].as<std::string>();
        swipe.proposalId = row["proposal_id"].as<std::string>();
        swipe.action = row["action"].as<std::string>();
        swipe.createdAt = row["created_at"].as<std::string>();
        return swipe;
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        if (isUniqueViolation(e))
        {
            throw SwipeConflictException("ALREADY_SWIPED",
                                          "You have already swiped on this Proposal.");
        }
        throw;
    }
}

}  // namespace services

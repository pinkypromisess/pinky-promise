#include "BlockService.h"

#include "../validation/SwipeValidation.h"  // reused for validation::isLikelyUuid

namespace services
{
Json::Value Block::toJson() const
{
    Json::Value j;
    j["id"] = id;
    j["blocker_user_id"] = blockerUserId;
    j["blocked_user_id"] = blockedUserId;
    j["created_at"] = createdAt;
    return j;
}

BlockService::BlockService(drogon::orm::DbClientPtr db) : db_(std::move(db))
{
}

BlockCreateResult BlockService::createBlock(const std::string &callerUserId,
                                             const std::string &blockedUserId)
{
    if (blockedUserId == callerUserId)
    {
        throw BlockBadRequestException("CANNOT_BLOCK_SELF", "You cannot block yourself.");
    }

    // A malformed id can't name a real user -- treat it exactly like
    // "unknown user" rather than letting Postgres reject the uuid bind
    // param (same pattern as SwipeService::recordSwipe's proposal-id
    // check). Per the E.1 brief this is 400 USER_NOT_FOUND, not 404 --
    // it's a body-field validation issue, not a resource-path one.
    if (!validation::isLikelyUuid(blockedUserId))
    {
        throw BlockBadRequestException("USER_NOT_FOUND", "No such user " + blockedUserId + ".");
    }

    // Existence check kept as a separate read before the atomic
    // insert+cascade statement below, rather than folded into it -- same
    // "check first, then act" shape ProposalService::createProposal uses
    // for its own precondition checks. A user deleted between this read
    // and the INSERT would surface as a foreign-key violation from the
    // statement below instead of USER_NOT_FOUND; acceptable for MVP scale
    // (mirrors the same non-atomic precondition style already used
    // elsewhere in this codebase).
    auto userRows = db_->execSqlSync("SELECT 1 FROM users WHERE id = $1", blockedUserId);
    if (userRows.empty())
    {
        throw BlockBadRequestException("USER_NOT_FOUND", "No such user " + blockedUserId + ".");
    }

    // One atomic statement:
    //  - existing_block: the caller's (caller -> blocked) row, if any.
    //  - inserted_block: INSERT ... SELECT ... WHERE NOT EXISTS(existing) --
    //    only fires when no row exists yet, so a re-block in the same
    //    direction is a no-op insert (idempotent), never a duplicate or a
    //    constraint-violation race (the UNIQUE (blocker_user_id,
    //    blocked_user_id) index still backs this as the real guard under
    //    concurrency).
    //  - close_conversations: only runs its effects when inserted_block
    //    actually fired (EXISTS check) -- an idempotent re-block must NOT
    //    re-run the cascade. Matches EITHER direction between the pair
    //    (proposer/interested can be either user), and closes both
    //    'active' AND 'pinky_promised' conversations -- see the code
    //    comment below on why 'pinky_promised' is included.
    //  - cancel_pinky_promises: cancels any 'confirmed' PinkyPromise whose
    //    conversation was just closed above (keyed off close_conversations'
    //    RETURNING id, so it only ever touches rows the cascade above
    //    actually closed).
    //  - proposals.status is deliberately never touched here -- blocking
    //    closes relationship touchpoints between the two users, it does
    //    not cancel a Proposal (contrast with DELETE /proposals/{id}).
    // The final SELECT always yields exactly one row (the caller's
    // existing row, or the one just inserted) via UNION ALL with a
    // WHERE NOT EXISTS guard on the second branch, same
    // data-modifying-CTEs-run-to-completion-in-one-statement idiom as
    // ProposalService::deleteProposal / PinkyPromiseService::confirm.
    auto rows = db_->execSqlSync(
        "WITH existing_block AS ("
        "  SELECT id, blocker_user_id, blocked_user_id, created_at FROM blocks "
        "  WHERE blocker_user_id = $1 AND blocked_user_id = $2"
        "), inserted_block AS ("
        "  INSERT INTO blocks (blocker_user_id, blocked_user_id) "
        "  SELECT $1, $2 WHERE NOT EXISTS (SELECT 1 FROM existing_block) "
        "  RETURNING id, blocker_user_id, blocked_user_id, created_at"
        "), close_conversations AS ("
        "  UPDATE conversations SET status = 'expired' "
        "  WHERE status IN ('active', 'pinky_promised') "
        "    AND EXISTS (SELECT 1 FROM inserted_block) "
        "    AND ((proposer_user_id = $1 AND interested_user_id = $2) "
        "      OR (proposer_user_id = $2 AND interested_user_id = $1)) "
        "  RETURNING id"
        "), cancel_pinky_promises AS ("
        "  UPDATE pinky_promises SET status = 'cancelled' "
        "  WHERE status = 'confirmed' "
        "    AND conversation_id IN (SELECT id FROM close_conversations)"
        ") "
        "SELECT id, blocker_user_id, blocked_user_id, created_at, true AS was_created "
        "FROM inserted_block "
        "UNION ALL "
        "SELECT id, blocker_user_id, blocked_user_id, created_at, false AS was_created "
        "FROM existing_block WHERE NOT EXISTS (SELECT 1 FROM inserted_block)",
        callerUserId,
        blockedUserId);

    const auto &row = rows[0];
    BlockCreateResult result;
    result.block.id = row["id"].as<std::string>();
    result.block.blockerUserId = row["blocker_user_id"].as<std::string>();
    result.block.blockedUserId = row["blocked_user_id"].as<std::string>();
    result.block.createdAt = row["created_at"].as<std::string>();
    result.wasCreated = row["was_created"].as<bool>();
    return result;
}

}  // namespace services

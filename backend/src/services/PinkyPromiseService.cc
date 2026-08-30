#include "PinkyPromiseService.h"

#include <drogon/orm/Exception.h>

#include <cstdint>
#include <vector>

#include "ConversationExpiry.h"
#include "../validation/SwipeValidation.h"  // reused for validation::isLikelyUuid

namespace services
{
namespace
{
std::chrono::system_clock::time_point epochToTp(int64_t epochSeconds)
{
    return std::chrono::system_clock::time_point(std::chrono::seconds(epochSeconds));
}

bool isUniqueViolation(const drogon::orm::DrogonDbException &e)
{
    const auto *sqlError = dynamic_cast<const drogon::orm::SqlError *>(&e.base());
    return sqlError != nullptr && sqlError->sqlState() == "23505";
}

PinkyPromise readPinkyPromise(const drogon::orm::Row &r)
{
    PinkyPromise pp;
    pp.id = r["id"].as<std::string>();
    pp.proposalId = r["proposal_id"].as<std::string>();
    pp.conversationId = r["conversation_id"].as<std::string>();
    pp.userAId = r["user_a_id"].as<std::string>();
    pp.userBId = r["user_b_id"].as<std::string>();
    pp.status = r["status"].as<std::string>();
    if (!r["confirmed_at"].isNull())
    {
        pp.confirmedAt = r["confirmed_at"].as<std::string>();
    }
    pp.createdAt = r["created_at"].as<std::string>();
    return pp;
}

}  // namespace

Json::Value PinkyPromise::toJson() const
{
    Json::Value j;
    j["id"] = id;
    j["proposal_id"] = proposalId;
    j["conversation_id"] = conversationId;
    j["user_a_id"] = userAId;
    j["user_b_id"] = userBId;
    j["status"] = status;
    j["confirmed_at"] = confirmedAt ? Json::Value(*confirmedAt) : Json::Value(Json::nullValue);
    j["created_at"] = createdAt;
    return j;
}

PinkyPromiseService::PinkyPromiseService(drogon::orm::DbClientPtr db) : db_(std::move(db))
{
}

bool PinkyPromiseService::conversationIsTimeExpired(
    const std::string &conversationId,
    std::chrono::system_clock::time_point createdAtTp,
    const std::string &status,
    const std::string &proposerUserId)
{
    auto msgRows = db_->execSqlSync(
        "SELECT sender_user_id, FLOOR(EXTRACT(EPOCH FROM created_at))::bigint AS e "
        "FROM messages WHERE conversation_id = $1 ORDER BY created_at ASC",
        conversationId);

    std::vector<ExpiryInputMessage> msgs;
    msgs.reserve(msgRows.size());
    for (const auto &r : msgRows)
    {
        msgs.push_back({r["sender_user_id"].as<std::string>(),
                        epochToTp(r["e"].as<int64_t>())});
    }

    const auto expiry = computeConversationExpiry(createdAtTp, status, proposerUserId, msgs);
    return expiry.has_value() && std::chrono::system_clock::now() > *expiry;
}

PinkyPromise PinkyPromiseService::initiate(const std::string &conversationId,
                                            const std::string &callerUserId)
{
    if (!validation::isLikelyUuid(conversationId))
    {
        throw NotFoundException("No conversation " + conversationId + ".");
    }

    auto convRows = db_->execSqlSync(
        "SELECT proposer_user_id, interested_user_id, proposal_id, status, "
        "FLOOR(EXTRACT(EPOCH FROM created_at))::bigint AS created_at_epoch "
        "FROM conversations WHERE id = $1",
        conversationId);
    if (convRows.empty())
    {
        throw NotFoundException("No conversation " + conversationId + ".");
    }

    const auto &c = convRows[0];
    const auto proposerUserId = c["proposer_user_id"].as<std::string>();
    const auto interestedUserId = c["interested_user_id"].as<std::string>();
    const auto proposalId = c["proposal_id"].as<std::string>();
    const auto convStatus = c["status"].as<std::string>();
    const auto convCreatedAtTp = epochToTp(c["created_at_epoch"].as<int64_t>());

    // Only A may initiate.
    if (callerUserId != proposerUserId)
    {
        if (callerUserId == interestedUserId)
        {
            throw PinkyPromiseForbiddenException(
                "NOT_INITIATOR", "Only the proposer can start a Pinky Promise.");
        }
        throw PinkyPromiseForbiddenException(
            "NOT_A_PARTICIPANT", "You are not a participant in this conversation.");
    }

    if (convStatus == "pinky_promised")
    {
        throw PinkyPromiseConflictException(
            "ALREADY_PINKY_PROMISED", "This conversation is already pinky-promised.");
    }
    // A DB status of 'expired' means the conversation was closed (e.g.
    // sibling closure); a still-'active' one is checked against its
    // computed expiry.
    if (convStatus == "expired" ||
        conversationIsTimeExpired(conversationId, convCreatedAtTp, convStatus, proposerUserId))
    {
        throw PinkyPromiseConflictException(
            "CONVERSATION_EXPIRED",
            "This conversation has expired; a Pinky Promise can no longer be started.");
    }

    auto existing = db_->execSqlSync(
        "SELECT 1 FROM pinky_promises WHERE conversation_id = $1 "
        "AND status IN ('pending_b_confirm', 'confirmed')",
        conversationId);
    if (!existing.empty())
    {
        throw PinkyPromiseConflictException(
            "PINKY_PROMISE_EXISTS", "A Pinky Promise already exists for this conversation.");
    }

    try
    {
        auto rows = db_->execSqlSync(
            "INSERT INTO pinky_promises "
            "  (proposal_id, conversation_id, user_a_id, user_b_id) "
            "VALUES ($1, $2, $3, $4) "
            "RETURNING id, proposal_id, conversation_id, user_a_id, user_b_id, status, "
            "confirmed_at, created_at",
            proposalId,
            conversationId,
            proposerUserId,
            interestedUserId);
        return readPinkyPromise(rows[0]);
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        // The partial UNIQUE (conversation_id) WHERE status IN
        // ('pending_b_confirm','confirmed') races with the pre-check above.
        if (isUniqueViolation(e))
        {
            throw PinkyPromiseConflictException(
                "PINKY_PROMISE_EXISTS",
                "A Pinky Promise already exists for this conversation.");
        }
        throw;
    }
}

PinkyPromise PinkyPromiseService::confirm(const std::string &pinkyPromiseId,
                                           const std::string &callerUserId)
{
    if (!validation::isLikelyUuid(pinkyPromiseId))
    {
        throw NotFoundException("No pinky promise " + pinkyPromiseId + ".");
    }

    auto ppRows = db_->execSqlSync(
        "SELECT pp.user_b_id, pp.status, pp.conversation_id, "
        "c.status AS conv_status, c.proposer_user_id AS conv_proposer, "
        "FLOOR(EXTRACT(EPOCH FROM c.created_at))::bigint AS conv_created_epoch "
        "FROM pinky_promises pp JOIN conversations c ON c.id = pp.conversation_id "
        "WHERE pp.id = $1",
        pinkyPromiseId);
    if (ppRows.empty())
    {
        throw NotFoundException("No pinky promise " + pinkyPromiseId + ".");
    }
    const auto &pp = ppRows[0];
    if (pp["user_b_id"].as<std::string>() != callerUserId)
    {
        throw PinkyPromiseForbiddenException(
            "NOT_CONFIRMER", "Only the invited user can confirm this Pinky Promise.");
    }
    if (pp["status"].as<std::string>() != "pending_b_confirm")
    {
        throw PinkyPromiseConflictException(
            "NOT_PENDING_CONFIRM", "This Pinky Promise is not awaiting confirmation.");
    }

    // The decay clock keeps running throughout pending_b_confirm (CUJ #4:
    // "if B never confirms, the normal decay clock keeps running"), so a
    // conversation that has since decayed past its computed expiry -- or
    // was explicitly closed (stored status 'expired') -- can no longer be
    // confirmed. Mirrors the same check in initiate(). conversationIsTime-
    // Expired() returns false for a 'pinky_promised' conversation, so this
    // never fires on the winning-side re-read.
    if (conversationIsTimeExpired(pp["conversation_id"].as<std::string>(),
                                   epochToTp(pp["conv_created_epoch"].as<int64_t>()),
                                   pp["conv_status"].as<std::string>(),
                                   pp["conv_proposer"].as<std::string>()))
    {
        throw PinkyPromiseConflictException(
            "CONVERSATION_EXPIRED",
            "This conversation has expired; the Pinky Promise can no longer be confirmed.");
    }

    // One atomic statement:
    //  - cap_a / cap_b count each party's CURRENT confirmed upcoming PPs
    //    (this PP is still 'pending_b_confirm', so it isn't counted);
    //  - confirmed_pp fires only if both caps are < 3 AND the PP is still
    //    pending AND the Proposal is still 'active' (guards the strict-1:1
    //    rule against a sibling conversation confirming first);
    //  - the three follow-on UPDATEs run to completion in the same
    //    statement (proposal -> pinky_promised [cross-table, Module B];
    //    winning conversation -> pinky_promised; sibling active
    //    conversations on the proposal -> expired).
    // The final SELECT always yields exactly one row; confirmed_pp columns
    // are NULL when the UPDATE did not fire.
    auto rows = db_->execSqlSync(
        "WITH pp AS ("
        "  SELECT proposal_id, conversation_id, user_a_id, user_b_id FROM pinky_promises "
        "  WHERE id = $1"
        "), cap_a AS ("
        "  SELECT count(*) AS c FROM pinky_promises x "
        "  JOIN proposals p ON p.id = x.proposal_id "
        "  WHERE x.status = 'confirmed' AND p.event_time > now() "
        "    AND (x.user_a_id = (SELECT user_a_id FROM pp) "
        "         OR x.user_b_id = (SELECT user_a_id FROM pp))"
        "), cap_b AS ("
        "  SELECT count(*) AS c FROM pinky_promises x "
        "  JOIN proposals p ON p.id = x.proposal_id "
        "  WHERE x.status = 'confirmed' AND p.event_time > now() "
        "    AND (x.user_a_id = (SELECT user_b_id FROM pp) "
        "         OR x.user_b_id = (SELECT user_b_id FROM pp))"
        "), confirmed_pp AS ("
        "  UPDATE pinky_promises "
        "  SET status = 'confirmed', confirmed_at = now() "
        "  WHERE id = $1 "
        "    AND status = 'pending_b_confirm' "
        // $2::int -- pin the bind-param type: `c` is bigint, so an
        // uncast $2 would be inferred int8 while drogon sends a 4-byte int.
        "    AND (SELECT c FROM cap_a) < $2::int "
        "    AND (SELECT c FROM cap_b) < $2::int "
        "    AND (SELECT status FROM proposals WHERE id = (SELECT proposal_id FROM pp)) "
        "        = 'active' "
        "  RETURNING id, proposal_id, conversation_id, user_a_id, user_b_id, status, "
        "            confirmed_at, created_at"
        "), upd_proposal AS ("
        "  UPDATE proposals SET status = 'pinky_promised' "
        "  WHERE id = (SELECT proposal_id FROM confirmed_pp)"
        "), upd_winning_conversation AS ("
        "  UPDATE conversations SET status = 'pinky_promised' "
        "  WHERE id = (SELECT conversation_id FROM confirmed_pp)"
        "), close_sibling_conversations AS ("
        "  UPDATE conversations SET status = 'expired' "
        "  WHERE proposal_id = (SELECT proposal_id FROM confirmed_pp) "
        "    AND id <> (SELECT conversation_id FROM confirmed_pp) "
        "    AND status = 'active'"
        ") "
        // No FROM clause -> exactly one result row. Each confirmed_pp
        // column comes back via a scalar subquery: the value when the
        // UPDATE fired, NULL when it did not.
        "SELECT (SELECT c FROM cap_a) AS cap_a_count, "
        "       (SELECT c FROM cap_b) AS cap_b_count, "
        "       (SELECT status FROM proposals WHERE id = (SELECT proposal_id FROM pp)) "
        "         AS proposal_status, "
        "       (SELECT id              FROM confirmed_pp) AS id, "
        "       (SELECT proposal_id     FROM confirmed_pp) AS proposal_id, "
        "       (SELECT conversation_id FROM confirmed_pp) AS conversation_id, "
        "       (SELECT user_a_id       FROM confirmed_pp) AS user_a_id, "
        "       (SELECT user_b_id       FROM confirmed_pp) AS user_b_id, "
        "       (SELECT status          FROM confirmed_pp) AS status, "
        "       (SELECT confirmed_at    FROM confirmed_pp) AS confirmed_at, "
        "       (SELECT created_at      FROM confirmed_pp) AS created_at",
        pinkyPromiseId,
        kMaxConcurrentConfirmed);

    const auto &r = rows[0];
    if (r["id"].isNull())
    {
        const auto capA = r["cap_a_count"].as<int64_t>();
        const auto capB = r["cap_b_count"].as<int64_t>();
        const auto proposalStatus =
            r["proposal_status"].isNull() ? std::string() : r["proposal_status"].as<std::string>();

        if (capA >= kMaxConcurrentConfirmed)
        {
            throw PinkyPromiseConflictException(
                "PINKY_PROMISE_CAP_REACHED",
                "The initiator already has " + std::to_string(kMaxConcurrentConfirmed) +
                    " confirmed upcoming Pinky Promises.");
        }
        if (capB >= kMaxConcurrentConfirmed)
        {
            throw PinkyPromiseConflictException(
                "PINKY_PROMISE_CAP_REACHED",
                "You already have " + std::to_string(kMaxConcurrentConfirmed) +
                    " confirmed upcoming Pinky Promises.");
        }
        if (proposalStatus != "active")
        {
            throw PinkyPromiseConflictException(
                "ALREADY_PINKY_PROMISED",
                "This event already has a confirmed Pinky Promise.");
        }
        // Status changed out from under us between the load and here.
        throw PinkyPromiseConflictException(
            "NOT_PENDING_CONFIRM", "This Pinky Promise is not awaiting confirmation.");
    }

    return readPinkyPromise(r);
}

}  // namespace services

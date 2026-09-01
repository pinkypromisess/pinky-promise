#include "ConversationService.h"

#include "../storage/TimeUtils.h"
#include "../validation/SwipeValidation.h"  // reused for validation::isLikelyUuid

namespace services
{
namespace
{
std::chrono::system_clock::time_point epochToTp(int64_t epochSeconds)
{
    return std::chrono::system_clock::time_point(std::chrono::seconds(epochSeconds));
}

}  // namespace

Json::Value ConversationView::toJson() const
{
    Json::Value j;
    j["id"] = id;
    j["proposal_id"] = proposalId;
    j["proposer_user_id"] = proposerUserId;
    j["interested_user_id"] = interestedUserId;
    j["last_activity_at"] = lastActivityAt;
    j["last_sender_id"] =
        lastSenderId ? Json::Value(*lastSenderId) : Json::Value(Json::nullValue);
    j["status"] = status;
    j["created_at"] = createdAt;
    j["expires_at"] = expiresAt ? Json::Value(*expiresAt) : Json::Value(Json::nullValue);
    return j;
}

Json::Value Message::toJson() const
{
    Json::Value j;
    j["id"] = id;
    j["conversation_id"] = conversationId;
    j["sender_user_id"] = senderUserId;
    j["type"] = type;
    j["content"] = content;
    j["created_at"] = createdAt;
    return j;
}

ConversationService::ConversationService(drogon::orm::DbClientPtr db) : db_(std::move(db))
{
}

ConversationService::ConversationRow ConversationService::parseConversationRow(
    const drogon::orm::Row &r)
{
    ConversationRow row;
    row.id = r["id"].as<std::string>();
    row.proposalId = r["proposal_id"].as<std::string>();
    row.proposerUserId = r["proposer_user_id"].as<std::string>();
    row.interestedUserId = r["interested_user_id"].as<std::string>();
    row.lastActivityAt = r["last_activity_at"].as<std::string>();
    if (!r["last_sender_id"].isNull())
    {
        row.lastSenderId = r["last_sender_id"].as<std::string>();
    }
    row.status = r["status"].as<std::string>();
    row.createdAt = r["created_at"].as<std::string>();
    row.createdAtTp = epochToTp(r["created_at_epoch"].as<int64_t>());
    return row;
}

std::optional<ConversationService::ConversationRow> ConversationService::loadConversation(
    const std::string &conversationId)
{
    if (!validation::isLikelyUuid(conversationId))
    {
        return std::nullopt;
    }

    auto rows = db_->execSqlSync(
        "SELECT id, proposal_id, proposer_user_id, interested_user_id, last_activity_at, "
        "last_sender_id, status, created_at, FLOOR(EXTRACT(EPOCH FROM created_at))::bigint AS "
        "created_at_epoch FROM conversations WHERE id = $1",
        conversationId);
    if (rows.empty())
    {
        return std::nullopt;
    }
    return parseConversationRow(rows[0]);
}

std::vector<ExpiryInputMessage> ConversationService::loadExpiryMessages(
    const std::string &conversationId)
{
    auto rows = db_->execSqlSync(
        "SELECT sender_user_id, FLOOR(EXTRACT(EPOCH FROM created_at))::bigint AS created_at_epoch "
        "FROM messages WHERE conversation_id = $1 ORDER BY created_at ASC",
        conversationId);

    std::vector<ExpiryInputMessage> msgs;
    msgs.reserve(rows.size());
    for (const auto &r : rows)
    {
        msgs.push_back({r["sender_user_id"].as<std::string>(),
                        epochToTp(r["created_at_epoch"].as<int64_t>())});
    }
    return msgs;
}

ConversationView ConversationService::toView(const ConversationRow &row)
{
    ConversationView v;
    v.id = row.id;
    v.proposalId = row.proposalId;
    v.proposerUserId = row.proposerUserId;
    v.interestedUserId = row.interestedUserId;
    v.lastActivityAt = row.lastActivityAt;
    v.lastSenderId = row.lastSenderId;
    v.status = row.status;
    v.createdAt = row.createdAt;

    const auto expiry = computeConversationExpiry(
        row.createdAtTp, row.status, row.proposerUserId, loadExpiryMessages(row.id));
    if (expiry)
    {
        v.expiresAt = storage::formatIso8601Utc(*expiry);
    }
    return v;
}

std::vector<ConversationView> ConversationService::listForUser(const std::string &userId)
{
    auto rows = db_->execSqlSync(
        "SELECT id, proposal_id, proposer_user_id, interested_user_id, last_activity_at, "
        "last_sender_id, status, created_at, FLOOR(EXTRACT(EPOCH FROM created_at))::bigint AS "
        "created_at_epoch FROM conversations "
        "WHERE proposer_user_id = $1 OR interested_user_id = $1 "
        "ORDER BY last_activity_at DESC",
        userId);

    std::vector<ConversationView> out;
    out.reserve(rows.size());
    for (const auto &r : rows)
    {
        out.push_back(toView(parseConversationRow(r)));
    }
    return out;
}

ConversationView ConversationService::getForUser(const std::string &conversationId,
                                                  const std::string &userId)
{
    auto rowOpt = loadConversation(conversationId);
    if (!rowOpt)
    {
        throw NotFoundException("No conversation " + conversationId + ".");
    }
    if (userId != rowOpt->proposerUserId && userId != rowOpt->interestedUserId)
    {
        throw ConversationForbiddenException("You are not a participant in this conversation.");
    }
    return toView(*rowOpt);
}

Message ConversationService::postMessage(const std::string &conversationId,
                                          const std::string &userId,
                                          const std::string &type,
                                          const std::string &content)
{
    auto rowOpt = loadConversation(conversationId);
    if (!rowOpt)
    {
        throw NotFoundException("No conversation " + conversationId + ".");
    }
    const auto &row = *rowOpt;
    if (userId != row.proposerUserId && userId != row.interestedUserId)
    {
        throw ConversationForbiddenException("You are not a participant in this conversation.");
    }

    // A `pinky_promised` conversation never expires; every other status
    // is checked against the COMPUTED expiry, not any stored flag.
    if (row.status != "pinky_promised")
    {
        const auto expiry = computeConversationExpiry(
            row.createdAtTp, row.status, row.proposerUserId, loadExpiryMessages(row.id));
        if (expiry && std::chrono::system_clock::now() > *expiry)
        {
            throw ConversationExpiredException();
        }
    }

    // One atomic statement: insert the message, then bump the
    // conversation's last_activity_at (to the new message's timestamp)
    // and last_sender_id. Data-modifying CTEs always run to completion.
    auto rows = db_->execSqlSync(
        "WITH new_msg AS ("
        "  INSERT INTO messages (conversation_id, sender_user_id, type, content_or_url) "
        "  VALUES ($1, $2, $3, $4) "
        "  RETURNING id, conversation_id, sender_user_id, type, content_or_url, created_at"
        "), bump AS ("
        "  UPDATE conversations "
        "  SET last_activity_at = (SELECT created_at FROM new_msg), last_sender_id = $2 "
        "  WHERE id = $1"
        ") "
        "SELECT id, conversation_id, sender_user_id, type, content_or_url, created_at "
        "FROM new_msg",
        conversationId,
        userId,
        type,
        content);

    const auto &m = rows[0];
    Message msg;
    msg.id = m["id"].as<std::string>();
    msg.conversationId = m["conversation_id"].as<std::string>();
    msg.senderUserId = m["sender_user_id"].as<std::string>();
    msg.type = m["type"].as<std::string>();
    msg.content = m["content_or_url"].as<std::string>();
    msg.createdAt = m["created_at"].as<std::string>();
    return msg;
}

}  // namespace services

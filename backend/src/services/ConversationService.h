#pragma once

#include <drogon/orm/DbClient.h>
#include <drogon/orm/Row.h>
#include <json/json.h>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "ConversationExpiry.h"
#include "ConversationServiceErrors.h"
#include "ServiceErrors.h"

namespace services
{
// A Conversation as returned by the API. `expiresAt` is COMPUTED on read
// from `createdAt` + the ordered message list (see ConversationExpiry.h);
// it is std::nullopt when the conversation is `pinky_promised`.
struct ConversationView
{
    std::string id;
    std::string proposalId;
    std::string proposerUserId;
    std::string interestedUserId;
    std::string lastActivityAt;
    std::optional<std::string> lastSenderId;
    std::string status;
    std::string createdAt;
    std::optional<std::string> expiresAt;

    Json::Value toJson() const;
};

struct Message
{
    std::string id;
    std::string conversationId;
    std::string senderUserId;
    std::string type;
    std::string content;  // maps to the `content_or_url` column
    std::string createdAt;

    Json::Value toJson() const;
};

// Owns Conversation + Message reads/writes and the computed-expiry
// projection. All DB calls are synchronous (execSqlSync), matching the
// other services in this backend. Conversation *creation* is not here --
// it is folded into the `interested`-swipe INSERT in SwipeService so the
// swipe and its conversation cannot diverge (CUJ #4).
class ConversationService
{
  public:
    explicit ConversationService(drogon::orm::DbClientPtr db);

    // Conversations where `userId` is the proposer OR the interested
    // user, most-recent `last_activity_at` first.
    std::vector<ConversationView> listForUser(const std::string &userId);

    // Throws NotFoundException (404) if the id is malformed or names no
    // conversation; ConversationForbiddenException (403) if `userId` is
    // not a participant.
    ConversationView getForUser(const std::string &conversationId, const std::string &userId);

    // 404 / 403 as above, plus ConversationExpiredException (409) when the
    // computed status is `expired`. On success: inserts the message and
    // bumps `last_activity_at` + `last_sender_id` in one atomic
    // statement, then returns the created message.
    Message postMessage(const std::string &conversationId,
                         const std::string &userId,
                         const std::string &type,
                         const std::string &content);

    // 404 / 403 as above (same participant gate as postMessage/getForUser
    // — no expiry check, an expired or pinky_promised conversation's
    // history is still readable). Messages ordered oldest-first
    // (created_at ASC), matching loadExpiryMessages' ordering. Added for
    // Frontend Module 3, which otherwise has no way to render a
    // conversation's message thread at all — C.2 shipped POST but not
    // GET for this path.
    std::vector<Message> listMessages(const std::string &conversationId,
                                       const std::string &userId);

  private:
    struct ConversationRow
    {
        std::string id;
        std::string proposalId;
        std::string proposerUserId;
        std::string interestedUserId;
        std::string lastActivityAt;
        std::optional<std::string> lastSenderId;
        std::string status;
        std::string createdAt;
        std::chrono::system_clock::time_point createdAtTp;
    };

    static ConversationRow parseConversationRow(const drogon::orm::Row &r);

    // std::nullopt when the id is malformed or matches no row.
    std::optional<ConversationRow> loadConversation(const std::string &conversationId);

    std::vector<ExpiryInputMessage> loadExpiryMessages(const std::string &conversationId);

    // Builds the API view, computing `expiresAt` from the row + messages.
    ConversationView toView(const ConversationRow &row);

    drogon::orm::DbClientPtr db_;
};

}  // namespace services

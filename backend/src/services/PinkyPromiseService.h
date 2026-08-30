#pragma once

#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include <chrono>
#include <optional>
#include <string>

#include "PinkyPromiseServiceErrors.h"
#include "ServiceErrors.h"

namespace services
{
struct PinkyPromise
{
    std::string id;
    std::string proposalId;
    std::string conversationId;
    std::string userAId;
    std::string userBId;
    std::string status;
    std::optional<std::string> confirmedAt;
    std::string createdAt;

    Json::Value toJson() const;
};

// Owns PinkyPromise persistence and the initiate / confirm transitions
// (CUJ #4, entities doc Section 5). All DB calls are synchronous
// (execSqlSync), matching the other services.
//
// The confirm transition performs, in ONE atomic multi-CTE statement:
//   1. this PinkyPromise      -> status='confirmed', confirmed_at=now()
//   2. its Proposal           -> status='pinky_promised'  (cross-table write, Module B)
//   3. its winning Conversation-> status='pinky_promised'
//   4. sibling active Conversations on the same Proposal -> status='expired'
class PinkyPromiseService
{
  public:
    explicit PinkyPromiseService(drogon::orm::DbClientPtr db);

    // A initiates on a Conversation. Throws:
    //  - NotFoundException (404)                    conversation missing / malformed id
    //  - PinkyPromiseForbiddenException (403)       NOT_INITIATOR / NOT_A_PARTICIPANT
    //  - PinkyPromiseConflictException (409)        ALREADY_PINKY_PROMISED /
    //                                               CONVERSATION_EXPIRED / PINKY_PROMISE_EXISTS
    PinkyPromise initiate(const std::string &conversationId, const std::string &callerUserId);

    // B confirms. Throws:
    //  - NotFoundException (404)                    PP missing / malformed id
    //  - PinkyPromiseForbiddenException (403)       NOT_CONFIRMER
    //  - PinkyPromiseConflictException (409)        NOT_PENDING_CONFIRM /
    //                                               CONVERSATION_EXPIRED /
    //                                               ALREADY_PINKY_PROMISED /
    //                                               PINKY_PROMISE_CAP_REACHED
    PinkyPromise confirm(const std::string &pinkyPromiseId, const std::string &callerUserId);

  private:
    static constexpr int kMaxConcurrentConfirmed = 3;

    // Recomputes the Conversation's expiry (reusing the pure
    // services::computeConversationExpiry) and returns true when it is in
    // the past. `createdAtTp` / `status` / `proposerUserId` are the
    // already-loaded conversation fields.
    bool conversationIsTimeExpired(const std::string &conversationId,
                                    std::chrono::system_clock::time_point createdAtTp,
                                    const std::string &status,
                                    const std::string &proposerUserId);

    drogon::orm::DbClientPtr db_;
};

}  // namespace services

#pragma once

#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include <string>

#include "BlockServiceErrors.h"
#include "ServiceErrors.h"

namespace services
{
struct Block
{
    std::string id;
    std::string blockerUserId;
    std::string blockedUserId;
    std::string createdAt;

    Json::Value toJson() const;
};

// Returned by createBlock() so the controller can pick 200 (idempotent
// re-block) vs 201 (new block, cascade ran) without a second query.
struct BlockCreateResult
{
    Block block;
    bool wasCreated = false;
};

// Owns Block persistence and the block-time cascade (CUJ #10). Every DB
// call is synchronous (drogon::orm::DbClient::execSqlSync), matching
// ProposalService / SwipeService / PinkyPromiseService.
class BlockService
{
  public:
    explicit BlockService(drogon::orm::DbClientPtr db);

    // Creates a block from `callerUserId` -> `blockedUserId`.
    //
    // Throws:
    //  - BlockBadRequestException (400, CANNOT_BLOCK_SELF) if
    //    blockedUserId == callerUserId.
    //  - BlockBadRequestException (400, USER_NOT_FOUND) if blockedUserId
    //    is malformed or doesn't name a real user.
    //
    // If a block row for this exact (caller -> blocked) direction already
    // exists, this is idempotent: returns the existing row with
    // wasCreated = false and does NOT re-run the cascade below (it already
    // ran when the block was first created).
    //
    // Otherwise inserts the block row and, in the SAME atomic statement,
    // runs the block cascade between the two users (either direction):
    //   - any conversations row with status IN ('active','pinky_promised')
    //     between the pair -> 'expired'
    //   - any pinky_promises row with status = 'confirmed' on those
    //     conversations -> 'cancelled'
    // proposals.status is left untouched (blocking doesn't delete a
    // proposal). Returns wasCreated = true in this branch.
    BlockCreateResult createBlock(const std::string &callerUserId,
                                   const std::string &blockedUserId);

  private:
    drogon::orm::DbClientPtr db_;
};

}  // namespace services

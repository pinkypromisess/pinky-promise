#pragma once

#include <stdexcept>
#include <string>

namespace services
{
// PinkyPromise endpoints -> 403. `code` distinguishes the cases:
//   NOT_INITIATOR      -- B (or some non-A participant) tried to initiate.
//   NOT_A_PARTICIPANT  -- caller isn't in the conversation at all
//                         (consistent with ConversationService).
//   NOT_CONFIRMER      -- caller isn't `user_b_id` of the PinkyPromise.
class PinkyPromiseForbiddenException : public std::runtime_error
{
  public:
    PinkyPromiseForbiddenException(std::string code, std::string message)
      : std::runtime_error(std::move(message)), code(std::move(code))
    {
    }

    std::string code;
};

// PinkyPromise endpoints -> 409. `code` distinguishes the cases:
//   ALREADY_PINKY_PROMISED   -- the Conversation / Proposal is already promised.
//   CONVERSATION_EXPIRED     -- the Conversation's computed status is expired.
//   PINKY_PROMISE_EXISTS     -- a live PinkyPromise already exists for this Conversation.
//   NOT_PENDING_CONFIRM      -- the PinkyPromise is not in `pending_b_confirm`.
//   PINKY_PROMISE_CAP_REACHED-- A or B already has 3 confirmed upcoming PinkyPromises.
class PinkyPromiseConflictException : public std::runtime_error
{
  public:
    PinkyPromiseConflictException(std::string code, std::string message)
      : std::runtime_error(std::move(message)), code(std::move(code))
    {
    }

    std::string code;
};

}  // namespace services

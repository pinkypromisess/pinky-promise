#pragma once

#include <stdexcept>
#include <string>

namespace services
{
// GET/POST on /v1/conversations/{id}* -> 403. The caller is neither the
// proposer nor the interested user of this Conversation. (Chosen over 404
// so the two cases -- "no such conversation" vs "not yours" -- stay
// distinguishable; applied consistently to GET one and POST message.)
class ConversationForbiddenException : public std::runtime_error
{
  public:
    explicit ConversationForbiddenException(std::string message)
      : std::runtime_error(std::move(message))
    {
    }
};

// POST /v1/conversations/{id}/messages -> 409. The conversation's
// COMPUTED status is `expired` (now > computed expires_at and status is
// not `pinky_promised`). A `pinky_promised` conversation never hits this.
class ConversationExpiredException : public std::runtime_error
{
  public:
    ConversationExpiredException()
      : std::runtime_error("This conversation has expired; no new messages can be sent.")
    {
    }
};

}  // namespace services

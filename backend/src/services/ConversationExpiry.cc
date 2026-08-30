#include "ConversationExpiry.h"

#include <algorithm>

namespace services
{
namespace
{
using Clock = std::chrono::system_clock;

constexpr auto kMaxLifetime = std::chrono::hours(72);   // 3 days from creation
constexpr auto kBaseLifetime = std::chrono::hours(12);  // from A's first reply
constexpr auto kSameSenderBonus = std::chrono::minutes(1);
constexpr auto kOtherSideBonus = std::chrono::hours(2);

}  // namespace

std::optional<Clock::time_point> computeConversationExpiry(
    Clock::time_point conversationCreatedAt,
    const std::string &status,
    const std::string &proposerUserId,
    const std::vector<ExpiryInputMessage> &messagesOrderedByCreatedAtAsc)
{
    if (status == "pinky_promised")
    {
        return std::nullopt;
    }

    // An explicitly-closed conversation (e.g. a sibling closed when
    // another conversation on the same proposal reached a confirmed Pinky
    // Promise -- migration 008 / PinkyPromiseService) is already over:
    // report its expiry as its creation time so both GET (`expires_at` in
    // the past) and POST (the `now > expires_at` guard) treat it as
    // expired. The time-decay formula below still owns the *live* case;
    // this only short-circuits a terminal stored status.
    if (status == "expired")
    {
        return conversationCreatedAt;
    }

    const auto hardCap = conversationCreatedAt + kMaxLifetime;

    // Find the index of A's first message (plain loop -- no lambda).
    size_t aFirstIdx = messagesOrderedByCreatedAtAsc.size();
    for (size_t i = 0; i < messagesOrderedByCreatedAtAsc.size(); ++i)
    {
        if (messagesOrderedByCreatedAtAsc[i].senderUserId == proposerUserId)
        {
            aFirstIdx = i;
            break;
        }
    }

    if (aFirstIdx == messagesOrderedByCreatedAtAsc.size())
    {
        // A never replied -> flat 3 days from creation (messages B sent
        // into the void do not start the 12h clock).
        return hardCap;
    }

    auto expiry = messagesOrderedByCreatedAtAsc[aFirstIdx].createdAt + kBaseLifetime;

    // Walk only the messages AFTER A's first reply. `prevSender` starts as
    // A, since A "just spoke" with the aFirstIdx message.
    std::string prevSender = proposerUserId;
    for (size_t i = aFirstIdx + 1; i < messagesOrderedByCreatedAtAsc.size(); ++i)
    {
        const auto &sender = messagesOrderedByCreatedAtAsc[i].senderUserId;
        expiry += (sender == prevSender) ? kSameSenderBonus : kOtherSideBonus;
        prevSender = sender;
    }

    return std::min(expiry, hardCap);
}

}  // namespace services

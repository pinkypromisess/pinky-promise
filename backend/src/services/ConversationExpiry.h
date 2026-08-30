#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace services
{
// One message, reduced to just what the expiry formula needs.
struct ExpiryInputMessage
{
    std::string senderUserId;
    std::chrono::system_clock::time_point createdAt;
};

// Pure function -- no DB, no wall clock, no I/O. This is the manager's
// authoritative reading of CUJ #4 / the entities doc's Section 5:
//
//   status == "pinky_promised"  -> no expiry (std::nullopt)
//   A never replied             -> conversationCreatedAt + 3 days
//   otherwise:
//     base  = (A's first message time) + 12h
//     bonus = for each message AFTER A's first reply:
//               same sender as the previous message  -> + 1 minute
//               different sender                      -> + 2 hours
//     expiry = min(base + bonus, conversationCreatedAt + 3 days)
//
// Messages sent before A's first reply contribute NO bonus (the 12h clock
// doesn't exist yet; the flat 3-day clock applies until A speaks).
//
// `messagesOrderedByCreatedAtAsc` MUST already be sorted ascending by
// createdAt; this function does not sort.
std::optional<std::chrono::system_clock::time_point> computeConversationExpiry(
    std::chrono::system_clock::time_point conversationCreatedAt,
    const std::string &status,
    const std::string &proposerUserId,
    const std::vector<ExpiryInputMessage> &messagesOrderedByCreatedAtAsc);

}  // namespace services

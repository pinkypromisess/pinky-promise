#include <drogon/drogon_test.h>

#include <chrono>
#include <string>
#include <vector>

#include "../src/services/ConversationExpiry.h"

// Every test here is PURE-LOGIC: it calls services::computeConversationExpiry
// directly with hand-built inputs. No DB, no HTTP, no wall clock. This is
// the spec-conformance guard for the CUJ #4 / Section 5 expiry formula;
// the end-to-end coverage of the same rules over HTTP is in
// test_http_conversations.cc.

using namespace services;
using Clock = std::chrono::system_clock;

namespace
{
Clock::time_point tp(long long sec)
{
    return Clock::time_point(std::chrono::seconds(sec));
}

long long secs(Clock::time_point t)
{
    return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count();
}

const std::string A = "user-A-proposer";
const std::string B = "user-B-interested";

constexpr long long MIN = 60;
constexpr long long HOUR = 3600;
constexpr long long DAY = 86400;

}  // namespace

// CHECKS: a pinky_promised conversation has no expiry, regardless of messages.
DROGON_TEST(PinkyPromisedHasNoExpiry)
{
    CHECK(!computeConversationExpiry(tp(1000), "pinky_promised", A, {}).has_value());

    std::vector<ExpiryInputMessage> msgs{{A, tp(2000)}, {B, tp(3000)}};
    CHECK(!computeConversationExpiry(tp(1000), "pinky_promised", A, msgs).has_value());
}

// CHECKS: an explicitly-closed conversation (stored status "expired",
// e.g. sibling closure at PinkyPromise confirm) reports its expiry as its
// creation time -- already over -- regardless of messages.
DROGON_TEST(StoredExpiredStatusIsAlreadyOver)
{
    const long long created = 100 * DAY;
    std::vector<ExpiryInputMessage> msgs{{A, tp(created + 60)}, {B, tp(created + 120)}};

    auto exp = computeConversationExpiry(tp(created), "expired", A, msgs);
    REQUIRE(exp.has_value());
    CHECK(secs(*exp) == created);
}

// CHECKS: if A never replies, expiry is exactly created_at + 3 days
// (messages B sent "into the void" do not start the 12h clock).
DROGON_TEST(ANeverRepliedIsFlatThreeDays)
{
    const long long created = 10 * DAY;

    auto noMsgs = computeConversationExpiry(tp(created), "active", A, {});
    REQUIRE(noMsgs.has_value());
    CHECK(secs(*noMsgs) == created + 3 * DAY);

    std::vector<ExpiryInputMessage> onlyB{
        {B, tp(created + 100)}, {B, tp(created + 200)}, {B, tp(created + 300)}};
    auto withB = computeConversationExpiry(tp(created), "active", A, onlyB);
    REQUIRE(withB.has_value());
    CHECK(secs(*withB) == created + 3 * DAY);
}

// CHECKS: A's first reply sets base = that message + 12h, with no further
// messages contributing anything.
DROGON_TEST(AFirstReplySetsTwelveHourBase)
{
    const long long created = 100 * DAY;
    const long long aFirst = created + 5 * HOUR;

    std::vector<ExpiryInputMessage> msgs{{A, tp(aFirst)}};
    auto exp = computeConversationExpiry(tp(created), "active", A, msgs);
    REQUIRE(exp.has_value());
    CHECK(secs(*exp) == aFirst + 12 * HOUR);
}

// CHECKS: +1 minute for each same-sender repeat after A's first reply.
DROGON_TEST(SameSenderRepeatAddsOneMinuteEach)
{
    const long long created = 100 * DAY;
    const long long aFirst = created + HOUR;

    std::vector<ExpiryInputMessage> msgs{
        {A, tp(aFirst)}, {A, tp(aFirst + 10)}, {A, tp(aFirst + 20)}};
    auto exp = computeConversationExpiry(tp(created), "active", A, msgs);
    REQUIRE(exp.has_value());
    // base + 1min (2nd A) + 1min (3rd A)
    CHECK(secs(*exp) == aFirst + 12 * HOUR + 2 * MIN);
}

// CHECKS: +2 hours for each first-reply-after-the-other-side.
DROGON_TEST(OtherSideReplyAddsTwoHoursEach)
{
    const long long created = 100 * DAY;
    const long long aFirst = created + HOUR;

    std::vector<ExpiryInputMessage> msgs{
        {A, tp(aFirst)}, {B, tp(aFirst + 10)}, {A, tp(aFirst + 20)}};
    auto exp = computeConversationExpiry(tp(created), "active", A, msgs);
    REQUIRE(exp.has_value());
    // base + 2h (B after A) + 2h (A after B)
    CHECK(secs(*exp) == aFirst + 12 * HOUR + 4 * HOUR);
}

// CHECKS: a realistic mix of repeats and switches sums correctly.
DROGON_TEST(MixedRepeatsAndSwitchesSumCorrectly)
{
    const long long created = 100 * DAY;
    const long long aFirst = created + HOUR;

    std::vector<ExpiryInputMessage> msgs{
        {A, tp(aFirst)},           // A's first reply -> base
        {A, tp(aFirst + 10)},      // same as prev (A)  -> +1min
        {B, tp(aFirst + 20)},      // switch A->B       -> +2h
        {B, tp(aFirst + 30)},      // same as prev (B)  -> +1min
        {A, tp(aFirst + 40)},      // switch B->A       -> +2h
    };
    auto exp = computeConversationExpiry(tp(created), "active", A, msgs);
    REQUIRE(exp.has_value());
    CHECK(secs(*exp) == aFirst + 12 * HOUR + (2 * MIN + 4 * HOUR));
}

// CHECKS: messages sent BEFORE A's first reply are ignored entirely --
// they neither start the clock nor add bonus. Only messages strictly
// after A's first reply count.
DROGON_TEST(MessagesBeforeAFirstReplyContributeNothing)
{
    const long long created = 100 * DAY;
    const long long aFirst = created + 3 * HOUR;

    std::vector<ExpiryInputMessage> msgs{
        {B, tp(created + 60)},      // into the void
        {B, tp(created + 120)},     // into the void
        {A, tp(aFirst)},           // A's first reply -> base
        {B, tp(aFirst + 10)},      // switch A->B -> +2h
    };
    auto exp = computeConversationExpiry(tp(created), "active", A, msgs);
    REQUIRE(exp.has_value());
    CHECK(secs(*exp) == aFirst + 12 * HOUR + 2 * HOUR);
}

// CHECKS: the 3-day-from-creation hard cap wins even when base + bonus
// from heavy activity would push expiry well past it.
DROGON_TEST(ThreeDayCapWinsUnderHeavyActivity)
{
    const long long created = 100 * DAY;
    const long long aFirst = created + 1;

    std::vector<ExpiryInputMessage> msgs{{A, tp(aFirst)}};
    // 400 alternating messages -> ~400 * 2h of bonus, far beyond 3 days.
    for (int i = 1; i <= 400; ++i)
    {
        msgs.push_back({(i % 2 == 1) ? B : A, tp(aFirst + i)});
    }
    auto exp = computeConversationExpiry(tp(created), "active", A, msgs);
    REQUIRE(exp.has_value());
    CHECK(secs(*exp) == created + 3 * DAY);
}

// CHECKS: the cap also wins when A's first reply is itself so late that
// base alone (aFirst + 12h) already exceeds created_at + 3 days.
DROGON_TEST(ThreeDayCapWinsWhenBaseAloneExceedsIt)
{
    const long long created = 100 * DAY;
    const long long aFirst = created + 5 * DAY;  // A replies on day 5

    std::vector<ExpiryInputMessage> msgs{{A, tp(aFirst)}};
    auto exp = computeConversationExpiry(tp(created), "active", A, msgs);
    REQUIRE(exp.has_value());
    CHECK(secs(*exp) == created + 3 * DAY);
}

// CHECKS: the very first message being A's still counts as "A's first
// reply" (base), and the loop over subsequent messages starts from the
// one immediately after it.
DROGON_TEST(FirstMessageFromAIsTheBase)
{
    const long long created = 100 * DAY;

    std::vector<ExpiryInputMessage> msgs{{A, tp(created + 30)}, {A, tp(created + 40)}};
    auto exp = computeConversationExpiry(tp(created), "active", A, msgs);
    REQUIRE(exp.has_value());
    CHECK(secs(*exp) == (created + 30) + 12 * HOUR + 1 * MIN);
}

#pragma once

#include <json/json.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// Shared fixtures for tests that drive the real running server over HTTP
// (see TestHttpServer.h) rather than calling ProfileService/
// VerificationService directly.
namespace test_support
{
// Parses a "YYYY-MM-DDTHH:MM:SSZ" UTC string (the shape
// storage::formatIso8601Utc emits, e.g. the `expires_at` field) to epoch
// seconds. Returns -1 if it doesn't match that exact format. Uses a
// portable civil-days computation -- no timegm(), no locale, no TZ.
inline long long parseIso8601UtcToEpoch(const std::string &iso)
{
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%dZ", &y, &mo, &d, &h, &mi, &s) != 6)
    {
        return -1;
    }
    // Howard Hinnant's days_from_civil (1970-01-01 == day 0).
    long long yy = y - (mo <= 2);
    const long long era = (yy >= 0 ? yy : yy - 399) / 400;
    const long long yoe = yy - era * 400;
    const long long doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long long days = era * 146097 + doe - 719468;
    return days * 86400 + h * 3600 + mi * 60 + s;
}

// Absolute difference, in seconds, between two "...Z" UTC timestamp
// strings. Use as `CHECK(timestampSkewSeconds(expected, actual) <= 2)`
// wherever the compared value is derived (directly or transitively) from
// the real wall clock -- server column defaults, now()-relative seeds, DB
// round-trips: CI scheduler jitter makes exact wall-clock equality
// fragile, so assert magnitude within a small tolerance instead.
inline long long timestampSkewSeconds(const std::string &expectedIso,
                                       const std::string &actualIso)
{
    const long long e = parseIso8601UtcToEpoch(expectedIso);
    const long long a = parseIso8601UtcToEpoch(actualIso);
    if (e < 0 || a < 0)
    {
        return 1'000'000'000;  // force the CHECK to fail loudly on an unparseable input
    }
    return std::llabs(e - a);
}

struct TestSession
{
    std::string baseUrl;
    std::string token;
    std::string userId;
};

// Creates a fresh `users` row (the only unavoidable non-HTTP step — user
// signup/token issuance isn't part of this module's API surface) and
// starts/reuses the test server. Throws if the DB or server aren't
// reachable.
TestSession setUpTestSession();

Json::Value jsonStringArray(const std::vector<std::string> &values);

// PUT /v1/profile with the given photo URLs (all other fields fixed to a
// valid default). Throws (not FAIL/CHECK — see the .cc for why) if the
// response isn't 200.
Json::Value putProfileOverHttp(const TestSession &session, const std::vector<std::string> &photoUrls);

// Drives POST /v1/verification + GET /v1/verification/status and returns
// the resulting decision ("pass"/"fail"). Throws if either call doesn't
// return the expected status.
std::string verifyOverHttp(const TestSession &session);

// GET /v1/profile/me and return just `verified`. Throws if the response
// isn't 200.
bool getProfileVerifiedOverHttp(const TestSession &session);

}  // namespace test_support

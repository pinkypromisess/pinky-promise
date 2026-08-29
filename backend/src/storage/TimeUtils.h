#pragma once

#include <chrono>
#include <string>

namespace storage
{
// "20260820T194700Z" — GCS V4 signing's X-Goog-Date format.
std::string formatGoogDate(std::chrono::system_clock::time_point tp);

// "20260820" — the date component of a V4 credential scope.
std::string formatGoogDateStamp(std::chrono::system_clock::time_point tp);

// "2026-08-20T19:47:00Z" — RFC 3339 / ISO 8601, for the `expires_at`
// field returned to API clients.
std::string formatIso8601Utc(std::chrono::system_clock::time_point tp);

}  // namespace storage

#include "TimeUtils.h"

#include <array>
#include <ctime>

namespace storage
{
namespace
{
std::tm toUtcTm(std::chrono::system_clock::time_point tp)
{
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tmUtc{};
#if defined(_WIN32)
    gmtime_s(&tmUtc, &t);
#else
    gmtime_r(&t, &tmUtc);
#endif
    return tmUtc;
}

std::string strftimeUtc(std::chrono::system_clock::time_point tp, const char *format)
{
    const auto tmUtc = toUtcTm(tp);
    std::array<char, 32> buf{};
    const auto len = std::strftime(buf.data(), buf.size(), format, &tmUtc);
    return std::string(buf.data(), len);
}

}  // namespace

std::string formatGoogDate(std::chrono::system_clock::time_point tp)
{
    return strftimeUtc(tp, "%Y%m%dT%H%M%SZ");
}

std::string formatGoogDateStamp(std::chrono::system_clock::time_point tp)
{
    return strftimeUtc(tp, "%Y%m%d");
}

std::string formatIso8601Utc(std::chrono::system_clock::time_point tp)
{
    return strftimeUtc(tp, "%Y-%m-%dT%H:%M:%SZ");
}

}  // namespace storage

#include "TestDb.h"

#include <drogon/utils/Utilities.h>

#include <cstdlib>
#include <sstream>

namespace test_support
{
namespace
{
std::string envOr(const char *name, const std::string &fallback)
{
    if (const char *value = std::getenv(name))
    {
        return value;
    }
    return fallback;
}

std::string buildConnInfo()
{
    std::ostringstream oss;
    oss << "host=" << envOr("DB_HOST", "localhost") << " port=" << envOr("DB_PORT", "5432")
        << " dbname=" << envOr("DB_NAME", "pinky_promise") << " user=" << envOr("DB_USER", "postgres")
        << " password='" << envOr("DB_PASSWORD", "") << "'"
        << " client_encoding=utf8";
    return oss.str();
}

}  // namespace

drogon::orm::DbClientPtr testDbClient()
{
    static drogon::orm::DbClientPtr client =
        drogon::orm::DbClient::newPgClient(buildConnInfo(), /*connNum=*/2);
    return client;
}

std::string createTestUser(const drogon::orm::DbClientPtr &db)
{
    const auto userId = drogon::utils::getUuid();
    const auto email = "test-" + userId + "@example.com";
    db->execSqlSync("INSERT INTO users (id, email) VALUES ($1, $2)", userId, email);
    return userId;
}

std::vector<std::string> sixPhotoUrls(const std::string &prefix)
{
    std::vector<std::string> urls;
    for (int i = 1; i <= 6; ++i)
    {
        urls.push_back(prefix + std::to_string(i));
    }
    return urls;
}

validation::ProfileUpsertInput makeTestProfileInput(std::vector<std::string> photoUrls)
{
    validation::ProfileUpsertInput input;
    input.name = "Test User";
    input.sex = "female";
    input.age = 30;
    input.needToKnowText = "I love testing.";
    input.photoUrls = std::move(photoUrls);
    return input;
}

}  // namespace test_support

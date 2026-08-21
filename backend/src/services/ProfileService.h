#pragma once

#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include <optional>
#include <string>
#include <vector>

#include "../validation/ProfileValidation.h"

namespace services
{
struct Photo
{
    std::string id;
    std::string url;
    int position = 0;

    Json::Value toJson() const;
};

struct Profile
{
    std::string userId;
    std::string name;
    std::string sex;
    int age = 0;
    std::string needToKnowText;
    std::vector<Photo> photos;
    std::optional<std::string> occupation;
    std::optional<std::string> relationshipStatus;
    bool verified = false;
    std::string createdAt;  // ISO 8601, as returned by Postgres::to_iso

    Json::Value toJson() const;
};

// Owns Profile and profile-photo persistence. Every DB call is synchronous
// (drogon::orm::DbClient::execSqlSync) — acceptable for this MVP's request
// volume, and it keeps the multi-statement upsert/patch flows easy to read
// and keep transactional.
class ProfileService
{
  public:
    explicit ProfileService(drogon::orm::DbClientPtr db);

    // Throws services::ValidationFailedException on bad input.
    // Full upsert: replaces every field, including the photo set. If the
    // resulting photo URL list (order-sensitive) differs from what's
    // currently stored, `verified` is reset to false (CUJ #7).
    Profile upsertProfile(const std::string &userId,
                           const validation::ProfileUpsertInput &input);

    // Throws services::NotFoundException if the user has no profile yet.
    Profile getProfile(const std::string &userId);

    // Throws services::ValidationFailedException if the resulting photo
    // count would drop below the minimum, if `remove` references an id not
    // on the profile, or if the request is a no-op (both lists empty).
    // Throws services::NotFoundException if the user has no profile yet.
    // On success, unconditionally sets verified = false (CUJ #7).
    Profile patchPhotos(const std::string &userId,
                         const std::vector<std::string> &addUrls,
                         const std::vector<std::string> &removePhotoIds);

  private:
    drogon::orm::DbClientPtr db_;

    // Works against either the pooled client or an open transaction —
    // both expose the same execSqlSync interface via drogon::orm::DbClient.
    // Throws services::NotFoundException if the profile doesn't exist.
    static Profile loadProfileFrom(drogon::orm::DbClient &client, const std::string &userId);
};

}  // namespace services

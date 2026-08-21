#include "ProfileService.h"

#include <algorithm>
#include <set>

#include "ServiceErrors.h"

namespace services
{
Json::Value Photo::toJson() const
{
    Json::Value j;
    j["id"] = id;
    j["url"] = url;
    j["position"] = position;
    return j;
}

Json::Value Profile::toJson() const
{
    Json::Value j;
    j["user_id"] = userId;
    j["name"] = name;
    j["sex"] = sex;
    j["age"] = age;
    j["need_to_know_text"] = needToKnowText;

    Json::Value photosJson(Json::arrayValue);
    for (const auto &photo : photos)
    {
        photosJson.append(photo.toJson());
    }
    j["photos"] = photosJson;

    j["occupation"] = occupation ? Json::Value(*occupation) : Json::Value(Json::nullValue);
    j["relationship_status"] =
        relationshipStatus ? Json::Value(*relationshipStatus) : Json::Value(Json::nullValue);
    j["verified"] = verified;
    j["created_at"] = createdAt;
    return j;
}

ProfileService::ProfileService(drogon::orm::DbClientPtr db) : db_(std::move(db))
{
}

Profile ProfileService::loadProfileFrom(drogon::orm::DbClient &client, const std::string &userId)
{
    auto profileResult = client.execSqlSync(
        "SELECT user_id, name, sex, age, need_to_know_text, occupation, "
        "relationship_status, verified, created_at FROM profiles WHERE user_id = $1",
        userId);
    if (profileResult.empty())
    {
        throw NotFoundException("Profile not found for user " + userId);
    }
    const auto &row = profileResult[0];

    Profile profile;
    profile.userId = row["user_id"].as<std::string>();
    profile.name = row["name"].as<std::string>();
    profile.sex = row["sex"].as<std::string>();
    profile.age = row["age"].as<int>();
    profile.needToKnowText = row["need_to_know_text"].as<std::string>();
    if (!row["occupation"].isNull())
    {
        profile.occupation = row["occupation"].as<std::string>();
    }
    if (!row["relationship_status"].isNull())
    {
        profile.relationshipStatus = row["relationship_status"].as<std::string>();
    }
    profile.verified = row["verified"].as<bool>();
    profile.createdAt = row["created_at"].as<std::string>();

    auto photosResult = client.execSqlSync(
        "SELECT id, url, position FROM profile_photos WHERE user_id = $1 ORDER BY position",
        userId);
    profile.photos.reserve(photosResult.size());
    for (const auto &photoRow : photosResult)
    {
        profile.photos.push_back(Photo{photoRow["id"].as<std::string>(),
                                        photoRow["url"].as<std::string>(),
                                        photoRow["position"].as<int>()});
    }

    return profile;
}

Profile ProfileService::getProfile(const std::string &userId)
{
    return loadProfileFrom(*db_, userId);
}

Profile ProfileService::upsertProfile(const std::string &userId,
                                       const validation::ProfileUpsertInput &input)
{
    auto errors = validation::validateProfileUpsert(input);
    if (!errors.empty())
    {
        throw ValidationFailedException(std::move(errors));
    }

    auto transaction = db_->newTransaction();

    transaction->execSqlSync(
        "INSERT INTO profiles (user_id, name, sex, age, need_to_know_text, occupation, "
        "relationship_status) VALUES ($1, $2, $3, $4, $5, $6, $7) "
        "ON CONFLICT (user_id) DO UPDATE SET name = EXCLUDED.name, sex = EXCLUDED.sex, "
        "age = EXCLUDED.age, need_to_know_text = EXCLUDED.need_to_know_text, "
        "occupation = EXCLUDED.occupation, relationship_status = EXCLUDED.relationship_status",
        userId,
        input.name,
        input.sex,
        input.age,
        input.needToKnowText,
        input.occupation,
        input.relationshipStatus);

    auto currentPhotosResult = transaction->execSqlSync(
        "SELECT url FROM profile_photos WHERE user_id = $1 ORDER BY position", userId);
    std::vector<std::string> currentUrls;
    currentUrls.reserve(currentPhotosResult.size());
    for (const auto &row : currentPhotosResult)
    {
        currentUrls.push_back(row["url"].as<std::string>());
    }

    if (currentUrls != input.photoUrls)
    {
        transaction->execSqlSync("DELETE FROM profile_photos WHERE user_id = $1", userId);
        for (size_t i = 0; i < input.photoUrls.size(); ++i)
        {
            transaction->execSqlSync(
                "INSERT INTO profile_photos (user_id, url, position) VALUES ($1, $2, $3)",
                userId,
                input.photoUrls[i],
                static_cast<int>(i));
        }
        transaction->execSqlSync("UPDATE profiles SET verified = false WHERE user_id = $1",
                                  userId);
    }

    // drogon::orm::Transaction commits when its shared_ptr is destroyed,
    // but that commit is fired asynchronously (queued onto the
    // connection's event loop) rather than awaited — the transaction
    // going out of scope at the end of this function does NOT guarantee
    // the write has landed yet. A caller that immediately makes a
    // *separate* call on a different pooled connection (e.g. a second
    // HTTP request hitting GET /profile/me right after this one returns)
    // can otherwise race the commit and read stale data. Committing
    // explicitly and synchronously here closes that window.
    transaction->execSqlSync("COMMIT");

    return loadProfileFrom(*transaction, userId);
}

Profile ProfileService::patchPhotos(const std::string &userId,
                                     const std::vector<std::string> &addUrls,
                                     const std::vector<std::string> &removePhotoIds)
{
    if (addUrls.empty() && removePhotoIds.empty())
    {
        throw ValidationFailedException(
            {{"PHOTO_PATCH_NO_OP", "Request must add or remove at least one photo."}});
    }

    const std::set<std::string> removeSet(removePhotoIds.begin(), removePhotoIds.end());

    auto transaction = db_->newTransaction();

    auto profileExists =
        transaction->execSqlSync("SELECT 1 FROM profiles WHERE user_id = $1", userId);
    if (profileExists.empty())
    {
        throw NotFoundException("Profile not found for user " + userId);
    }

    auto currentPhotosResult = transaction->execSqlSync(
        "SELECT id, position FROM profile_photos WHERE user_id = $1 ORDER BY position", userId);

    std::set<std::string> currentIds;
    int maxPosition = -1;
    for (const auto &row : currentPhotosResult)
    {
        currentIds.insert(row["id"].as<std::string>());
        maxPosition = std::max(maxPosition, row["position"].as<int>());
    }

    for (const auto &removeId : removeSet)
    {
        if (currentIds.find(removeId) == currentIds.end())
        {
            throw ValidationFailedException(
                {{"PHOTO_NOT_FOUND", "Photo id " + removeId + " is not on this profile."}});
        }
    }

    const size_t resultingCount = currentIds.size() - removeSet.size() + addUrls.size();
    if (auto photoCountError = validation::validatePhotoCount(resultingCount))
    {
        throw ValidationFailedException({*photoCountError});
    }

    for (const auto &removeId : removeSet)
    {
        transaction->execSqlSync(
            "DELETE FROM profile_photos WHERE user_id = $1 AND id = $2", userId, removeId);
    }

    int nextPosition = maxPosition + 1;
    for (const auto &url : addUrls)
    {
        transaction->execSqlSync(
            "INSERT INTO profile_photos (user_id, url, position) VALUES ($1, $2, $3)",
            userId,
            url,
            nextPosition++);
    }

    transaction->execSqlSync("UPDATE profiles SET verified = false WHERE user_id = $1", userId);

    // See the matching comment in upsertProfile() — without this, a
    // caller's next request on a different connection can race the
    // transaction's async commit and read stale data.
    transaction->execSqlSync("COMMIT");

    return loadProfileFrom(*transaction, userId);
}

}  // namespace services

#include "ProposalService.h"

#include <algorithm>

namespace services
{
namespace
{
constexpr int kFeedLimit = 50;

bool contains(const std::vector<std::string> &values, const std::string &value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

}  // namespace

Json::Value ProposalLocation::toJson() const
{
    Json::Value j;
    j["lat"] = lat;
    j["lng"] = lng;
    j["address"] = address;
    return j;
}

Json::Value Proposal::toJson() const
{
    Json::Value j;
    j["id"] = id;
    j["creator_user_id"] = creatorUserId;
    j["activity_text"] = activityText;
    j["event_time"] = eventTime;
    j["location"] = location.toJson();
    j["payment_type"] = paymentType;
    j["looking_for_text"] = lookingForText;

    Json::Value revealed(Json::arrayValue);
    for (const auto &field : revealedFields)
    {
        revealed.append(field);
    }
    j["revealed_fields"] = revealed;

    j["status"] = status;
    j["created_at"] = createdAt;
    return j;
}

Json::Value ProposalCardCreatorProfile::toJson() const
{
    Json::Value j;
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
    return j;
}

Json::Value ProposalFeedItem::toJson() const
{
    Json::Value j;
    j["proposal"] = proposal.toJson();
    j["creator"] = creator.toJson();
    return j;
}

ProposalService::ProposalService(drogon::orm::DbClientPtr db) : db_(std::move(db))
{
}

Proposal ProposalService::createProposal(const std::string &userId,
                                          const validation::ProposalCreateInput &input)
{
    auto errors = validation::validateProposalCreate(input);
    if (!errors.empty())
    {
        throw ValidationFailedException(std::move(errors));
    }

    auto profileResult = db_->execSqlSync(
        "SELECT verified, occupation, relationship_status FROM profiles WHERE user_id = $1",
        userId);

    bool verified = false;
    std::optional<std::string> occupation;
    std::optional<std::string> relationshipStatus;
    if (!profileResult.empty())
    {
        const auto &row = profileResult[0];
        verified = row["verified"].as<bool>();
        if (!row["occupation"].isNull())
        {
            occupation = row["occupation"].as<std::string>();
        }
        if (!row["relationship_status"].isNull())
        {
            relationshipStatus = row["relationship_status"].as<std::string>();
        }
    }

    // A missing profile is treated the same as an unverified one -- there
    // is nothing to post against either way.
    if (!verified)
    {
        throw PostingNotAllowedException("PROFILE_NOT_VERIFIED",
                                          "Verification is required to post a Proposal.");
    }

    // Independently re-checks the live photo minimum rather than trusting
    // `verified` alone, since photo count is a live minimum per CUJ #1,
    // not a one-time gate -- this is the same defense-in-depth the task
    // asked for even though, in practice, Module A's PATCH /profile/photos
    // already can't leave `verified = true` with fewer than the minimum.
    auto photoCountResult =
        db_->execSqlSync("SELECT count(*) AS c FROM profile_photos WHERE user_id = $1", userId);
    const auto photoCount = photoCountResult[0]["c"].as<int64_t>();
    if (photoCount < static_cast<int64_t>(validation::kMinPhotos))
    {
        throw PostingNotAllowedException(
            "PHOTO_MINIMUM_NOT_MET",
            "At least " + std::to_string(validation::kMinPhotos) +
                " photos are required to post a Proposal.");
    }

    for (const auto &field : input.revealedFields)
    {
        if (field == "occupation" && !occupation)
        {
            throw ValidationFailedException(
                {{"REVEALED_FIELD_NOT_FILLED",
                  "occupation cannot be revealed -- it isn't filled in on your profile."}});
        }
        if (field == "relationship_status" && !relationshipStatus)
        {
            throw ValidationFailedException(
                {{"REVEALED_FIELD_NOT_FILLED",
                  "relationship_status cannot be revealed -- it isn't filled in on your "
                  "profile."}});
        }
    }

    const bool revealOccupation = contains(input.revealedFields, "occupation");
    const bool revealRelationshipStatus = contains(input.revealedFields, "relationship_status");

    auto insertResult = db_->execSqlSync(
        "INSERT INTO proposals (creator_user_id, activity_text, event_time, location_lat, "
        "location_lng, location_address, payment_type, looking_for_text, reveal_occupation, "
        "reveal_relationship_status) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10) "
        "RETURNING id, status, created_at",
        userId,
        input.activityText,
        input.eventTime,
        input.location.lat,
        input.location.lng,
        input.location.address,
        input.paymentType,
        input.lookingForText,
        revealOccupation,
        revealRelationshipStatus);

    const auto &row = insertResult[0];

    Proposal proposal;
    proposal.id = row["id"].as<std::string>();
    proposal.creatorUserId = userId;
    proposal.activityText = input.activityText;
    proposal.eventTime = input.eventTime;
    proposal.location = {input.location.lat, input.location.lng, input.location.address};
    proposal.paymentType = input.paymentType;
    proposal.lookingForText = input.lookingForText;
    if (revealOccupation)
    {
        proposal.revealedFields.push_back("occupation");
    }
    if (revealRelationshipStatus)
    {
        proposal.revealedFields.push_back("relationship_status");
    }
    proposal.status = row["status"].as<std::string>();
    proposal.createdAt = row["created_at"].as<std::string>();

    return proposal;
}

ProposalCardCreatorProfile ProposalService::loadCardProfile(const std::string &creatorUserId,
                                                              bool revealOccupation,
                                                              bool revealRelationshipStatus)
{
    ProposalCardCreatorProfile card;

    auto profileResult = db_->execSqlSync(
        "SELECT name, sex, age, need_to_know_text, occupation, relationship_status "
        "FROM profiles WHERE user_id = $1",
        creatorUserId);
    if (!profileResult.empty())
    {
        const auto &row = profileResult[0];
        card.name = row["name"].as<std::string>();
        card.sex = row["sex"].as<std::string>();
        card.age = row["age"].as<int>();
        card.needToKnowText = row["need_to_know_text"].as<std::string>();
        if (revealOccupation && !row["occupation"].isNull())
        {
            card.occupation = row["occupation"].as<std::string>();
        }
        if (revealRelationshipStatus && !row["relationship_status"].isNull())
        {
            card.relationshipStatus = row["relationship_status"].as<std::string>();
        }
    }

    auto photosResult = db_->execSqlSync(
        "SELECT id, url, position FROM profile_photos WHERE user_id = $1 ORDER BY position",
        creatorUserId);
    card.photos.reserve(photosResult.size());
    for (const auto &photoRow : photosResult)
    {
        card.photos.push_back(Photo{photoRow["id"].as<std::string>(),
                                     photoRow["url"].as<std::string>(),
                                     photoRow["position"].as<int>()});
    }

    return card;
}

std::vector<ProposalFeedItem> ProposalService::getFeed(const std::string &userId,
                                                         std::optional<double> lat,
                                                         std::optional<double> lng)
{
    const std::string activeNotOwnClause =
        "SELECT id, creator_user_id, activity_text, event_time, location_lat, location_lng, "
        "location_address, payment_type, looking_for_text, reveal_occupation, "
        "reveal_relationship_status, status, created_at FROM proposals "
        "WHERE status = 'active' AND creator_user_id != $1 "
        // TODO(Module C): exclude proposals already swiped by the caller
        // (LEFT JOIN swipes ON swipes.proposal_id = proposals.id AND
        // swipes.swiper_user_id = $1 WHERE swipes.id IS NULL) once the
        // swipes table exists -- it doesn't in this repo yet, so the feed
        // currently can return proposals the caller already Hearted/passed.
        // TODO(Module E): exclude proposals from creators the caller has
        // blocked or is blocked by, once the blocks table exists.
        ;

    auto proposalsResult =
        (lat.has_value() && lng.has_value())
            ? db_->execSqlSync(
                  activeNotOwnClause +
                      "ORDER BY ((location_lat - $2) * (location_lat - $2) + "
                      "(location_lng - $3) * (location_lng - $3)) ASC, created_at DESC LIMIT " +
                      std::to_string(kFeedLimit),
                  userId,
                  *lat,
                  *lng)
            : db_->execSqlSync(
                  activeNotOwnClause + "ORDER BY created_at DESC LIMIT " +
                      std::to_string(kFeedLimit),
                  userId);

    std::vector<ProposalFeedItem> items;
    items.reserve(proposalsResult.size());
    for (const auto &row : proposalsResult)
    {
        ProposalFeedItem item;
        item.proposal.id = row["id"].as<std::string>();
        item.proposal.creatorUserId = row["creator_user_id"].as<std::string>();
        item.proposal.activityText = row["activity_text"].as<std::string>();
        item.proposal.eventTime = row["event_time"].as<std::string>();
        item.proposal.location.lat = row["location_lat"].as<double>();
        item.proposal.location.lng = row["location_lng"].as<double>();
        item.proposal.location.address = row["location_address"].as<std::string>();
        item.proposal.paymentType = row["payment_type"].as<std::string>();
        item.proposal.lookingForText = row["looking_for_text"].as<std::string>();
        const bool revealOccupation = row["reveal_occupation"].as<bool>();
        const bool revealRelationshipStatus = row["reveal_relationship_status"].as<bool>();
        if (revealOccupation)
        {
            item.proposal.revealedFields.push_back("occupation");
        }
        if (revealRelationshipStatus)
        {
            item.proposal.revealedFields.push_back("relationship_status");
        }
        item.proposal.status = row["status"].as<std::string>();
        item.proposal.createdAt = row["created_at"].as<std::string>();

        // One extra pair of queries per feed item (profile + photos) --
        // fine for MVP scale, matches ProfileService's own
        // load-photos-separately approach. Worth batching if the feed
        // grows a real pagination story later.
        item.creator = loadCardProfile(
            item.proposal.creatorUserId, revealOccupation, revealRelationshipStatus);

        items.push_back(std::move(item));
    }

    return items;
}

void ProposalService::deleteProposal(const std::string &userId, const std::string &proposalId)
{
    auto result = db_->execSqlSync(
        "UPDATE proposals SET status = 'cancelled' WHERE id = $1 AND creator_user_id = $2 "
        "RETURNING id",
        proposalId,
        userId);

    if (result.empty())
    {
        throw NotFoundException("No proposal " + proposalId + " owned by this caller.");
    }

    // TODO(Module C): cascade this cancellation -- close any open
    // Conversations on this Proposal, cancel a confirmed PinkyPromise, and
    // cancel the scheduled reminder job (CUJ #8). Those tables/jobs belong
    // to Module C and don't exist in this repo yet, so for now this only
    // flips Proposal.status.
}

}  // namespace services

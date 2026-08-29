#pragma once

#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include <optional>
#include <string>
#include <vector>

#include "ProfileService.h"  // reused for services::Photo, embedded in feed cards
#include "ProposalServiceErrors.h"
#include "ServiceErrors.h"
#include "../validation/ProposalValidation.h"

namespace services
{
struct ProposalLocation
{
    double lat = 0.0;
    double lng = 0.0;
    std::string address;

    Json::Value toJson() const;
};

struct Proposal
{
    std::string id;
    std::string creatorUserId;
    std::string activityText;
    std::string eventTime;
    ProposalLocation location;
    std::string paymentType;
    std::string lookingForText;
    std::vector<std::string> revealedFields;
    std::string status;
    std::string createdAt;

    Json::Value toJson() const;
};

// The subset of the creator's Profile shown on a feed card, computed per
// a specific Proposal's revealed_fields (CUJ #1/#3 visibility rule).
// Always carries the required-to-create Profile fields (name/sex/age/
// need_to_know_text/photos); occupation/relationship_status are unset
// unless both filled in on the creator's profile AND revealed on this
// Proposal.
struct ProposalCardCreatorProfile
{
    std::string name;
    std::string sex;
    int age = 0;
    std::string needToKnowText;
    std::vector<Photo> photos;
    std::optional<std::string> occupation;
    std::optional<std::string> relationshipStatus;

    Json::Value toJson() const;
};

struct ProposalFeedItem
{
    Proposal proposal;
    ProposalCardCreatorProfile creator;

    Json::Value toJson() const;
};

// Owns Proposal persistence and the feed query. Every DB call is
// synchronous (drogon::orm::DbClient::execSqlSync), matching
// ProfileService's approach -- acceptable for this MVP's request volume.
class ProposalService
{
  public:
    explicit ProposalService(drogon::orm::DbClientPtr db);

    // Throws services::PostingNotAllowedException (403) if the caller's
    // profile is missing, unverified, or has fewer than the minimum
    // required photos. Throws services::ValidationFailedException (400)
    // on bad field input, or on a revealed_fields entry the caller's
    // profile doesn't actually have filled in.
    Proposal createProposal(const std::string &userId,
                             const validation::ProposalCreateInput &input);

    // Active proposals excluding the caller's own. When both lat and lng
    // are given, ranks by distance from that point (ascending) with
    // recency as a tiebreaker; otherwise ranks by recency alone. See the
    // TODOs in the .cc for known gaps (swipe/block exclusion -- those
    // tables don't exist in this repo yet).
    std::vector<ProposalFeedItem> getFeed(const std::string &userId,
                                           std::optional<double> lat,
                                           std::optional<double> lng);

    // Throws services::NotFoundException (404) if no such proposal exists
    // or the caller isn't its creator (same response either way, so as
    // not to leak a proposal's existence to a non-owner). Allowed
    // regardless of the proposal's current status (CUJ #8). Sets
    // status = 'cancelled'. Cascading to Conversations/PinkyPromise is a
    // flagged TODO in the .cc -- those tables belong to Module C.
    void deleteProposal(const std::string &userId, const std::string &proposalId);

  private:
    drogon::orm::DbClientPtr db_;

    // Loads the feed-card view of `creatorUserId`'s profile, applying the
    // reveal flags read off this specific proposal's row.
    ProposalCardCreatorProfile loadCardProfile(const std::string &creatorUserId,
                                                bool revealOccupation,
                                                bool revealRelationshipStatus);
};

}  // namespace services

#pragma once

#include <stdexcept>
#include <string>

namespace services
{
// POST /proposals/{id}/swipe -> 400. The caller tried to swipe on their
// own Proposal. `code` is machine-readable for the client.
class SwipeBadRequestException : public std::runtime_error
{
  public:
    SwipeBadRequestException(std::string code, std::string message)
      : std::runtime_error(std::move(message)), code(std::move(code))
    {
    }

    std::string code;
};

// POST /proposals/{id}/swipe -> 403. Either the caller's profile isn't
// verified (code PROFILE_NOT_VERIFIED) or they've hit the rolling-24h
// "interested" cap (code INTERESTED_DAILY_CAP_REACHED). Distinct codes so
// the client can tell "go verify" from "come back later".
class SwipeForbiddenException : public std::runtime_error
{
  public:
    SwipeForbiddenException(std::string code, std::string message)
      : std::runtime_error(std::move(message)), code(std::move(code))
    {
    }

    std::string code;
};

// POST /proposals/{id}/swipe -> 409. This user already has a swipe row for
// this proposal (the UNIQUE (proposal_id, swiper_user_id) constraint). Not
// an upsert -- the first swipe stands.
class SwipeConflictException : public std::runtime_error
{
  public:
    SwipeConflictException(std::string code, std::string message)
      : std::runtime_error(std::move(message)), code(std::move(code))
    {
    }

    std::string code;
};

}  // namespace services

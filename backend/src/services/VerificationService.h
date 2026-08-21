#pragma once

#include <drogon/orm/DbClient.h>
#include <drogon/orm/Row.h>
#include <json/json.h>

#include <memory>
#include <optional>
#include <string>

#include "../verification/FaceVerificationProvider.h"

namespace services
{
struct Verification
{
    std::string id;
    std::string userId;
    std::string decision;  // "pending" | "pass" | "fail"
    std::optional<std::string> livenessSessionId;
    std::optional<double> livenessScore;
    std::optional<double> faceMatchScore;
    std::string submittedAt;
    std::optional<std::string> decidedAt;

    Json::Value toJson() const;
};

// Orchestrates CUJ #2: starts a liveness session with the vendor, and on
// polling, resolves a pending attempt into pass/fail by combining the
// vendor's liveness result with a CompareFaces check against the user's
// current profile photos. See verification::FaceVerificationProvider for
// the vendor abstraction.
class VerificationService
{
  public:
    // Confidence thresholds (0-100) a verification must clear on both axes
    // to result in `pass`. Chosen conservatively for MVP; revisit once
    // there's real pass/fail rate data.
    static constexpr double kLivenessConfidenceThreshold = 90.0;
    static constexpr double kFaceMatchThreshold = 80.0;

    VerificationService(drogon::orm::DbClientPtr db,
                        std::shared_ptr<verification::FaceVerificationProvider> provider);

    // Throws services::NotFoundException if the caller has no profile yet.
    // Throws services::ValidationFailedException if the profile has fewer
    // than the minimum required photos (nothing to compare against).
    Verification startVerification(const std::string &userId);

    // Throws services::NotFoundException if the caller has never submitted
    // a verification attempt. If the latest attempt is still `pending`,
    // polls the vendor and finalizes the decision when ready; otherwise
    // returns the already-decided attempt unchanged.
    Verification getStatus(const std::string &userId);

  private:
    drogon::orm::DbClientPtr db_;
    std::shared_ptr<verification::FaceVerificationProvider> provider_;

    static Verification rowToVerification(const drogon::orm::Row &row);
};

}  // namespace services

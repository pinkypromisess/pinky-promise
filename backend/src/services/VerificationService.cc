#include "VerificationService.h"

#include "ServiceErrors.h"
#include "../validation/ProfileValidation.h"

namespace services
{
Json::Value Verification::toJson() const
{
    Json::Value j;
    j["id"] = id;
    j["user_id"] = userId;
    j["decision"] = decision;
    j["liveness_session_id"] =
        livenessSessionId ? Json::Value(*livenessSessionId) : Json::Value(Json::nullValue);
    j["liveness_score"] = livenessScore ? Json::Value(*livenessScore) : Json::Value(Json::nullValue);
    j["face_match_score"] =
        faceMatchScore ? Json::Value(*faceMatchScore) : Json::Value(Json::nullValue);
    j["submitted_at"] = submittedAt;
    j["decided_at"] = decidedAt ? Json::Value(*decidedAt) : Json::Value(Json::nullValue);
    return j;
}

VerificationService::VerificationService(
    drogon::orm::DbClientPtr db, std::shared_ptr<verification::FaceVerificationProvider> provider)
  : db_(std::move(db)), provider_(std::move(provider))
{
}

Verification VerificationService::rowToVerification(const drogon::orm::Row &row)
{
    Verification v;
    v.id = row["id"].as<std::string>();
    v.userId = row["user_id"].as<std::string>();
    v.decision = row["decision"].as<std::string>();
    if (!row["liveness_session_id"].isNull())
    {
        v.livenessSessionId = row["liveness_session_id"].as<std::string>();
    }
    if (!row["liveness_score"].isNull())
    {
        v.livenessScore = row["liveness_score"].as<double>();
    }
    if (!row["face_match_score"].isNull())
    {
        v.faceMatchScore = row["face_match_score"].as<double>();
    }
    v.submittedAt = row["submitted_at"].as<std::string>();
    if (!row["decided_at"].isNull())
    {
        v.decidedAt = row["decided_at"].as<std::string>();
    }
    return v;
}

Verification VerificationService::startVerification(const std::string &userId)
{
    auto transaction = db_->newTransaction();

    auto profileExists =
        transaction->execSqlSync("SELECT 1 FROM profiles WHERE user_id = $1", userId);
    if (profileExists.empty())
    {
        throw NotFoundException("Profile not found for user " + userId);
    }

    auto photoCountResult = transaction->execSqlSync(
        "SELECT count(*) AS photo_count FROM profile_photos WHERE user_id = $1", userId);
    const auto photoCount =
        static_cast<size_t>(photoCountResult[0]["photo_count"].as<int64_t>());
    if (auto photoCountError = validation::validatePhotoCount(photoCount))
    {
        throw ValidationFailedException({*photoCountError});
    }

    auto session = provider_->createLivenessSession(userId);

    auto insertResult = transaction->execSqlSync(
        "INSERT INTO verifications (user_id, liveness_session_id, decision) "
        "VALUES ($1, $2, 'pending') "
        "RETURNING id, user_id, decision, liveness_session_id, liveness_score, "
        "face_match_score, submitted_at, decided_at",
        userId,
        session.sessionId);

    // See the comment on the equivalent line in ProfileService — without
    // an explicit synchronous commit here, a client that immediately
    // calls GET /verification/status right after this returns (the
    // expected polling flow) can race drogon::orm::Transaction's async
    // commit-on-destruction and see no verification row yet.
    transaction->execSqlSync("COMMIT");

    return rowToVerification(insertResult[0]);
}

Verification VerificationService::getStatus(const std::string &userId)
{
    auto transaction = db_->newTransaction();

    auto latest = transaction->execSqlSync(
        "SELECT id, user_id, decision, liveness_session_id, liveness_score, "
        "face_match_score, submitted_at, decided_at FROM verifications "
        "WHERE user_id = $1 ORDER BY submitted_at DESC LIMIT 1",
        userId);
    if (latest.empty())
    {
        throw NotFoundException("No verification attempt found for user " + userId);
    }

    auto current = rowToVerification(latest[0]);
    if (current.decision != "pending" || !current.livenessSessionId)
    {
        return current;
    }

    auto livenessResult = provider_->getLivenessResult(*current.livenessSessionId);
    if (livenessResult.status == verification::LivenessStatus::Pending)
    {
        return current;
    }

    if (livenessResult.status == verification::LivenessStatus::Failed)
    {
        auto updated = transaction->execSqlSync(
            "UPDATE verifications SET decision = 'fail', liveness_score = $2, "
            "decided_at = now() WHERE id = $1 "
            "RETURNING id, user_id, decision, liveness_session_id, liveness_score, "
            "face_match_score, submitted_at, decided_at",
            current.id,
            livenessResult.confidence);
        // See the comment on startVerification()'s commit for why this
        // can't be left to the transaction's async destructor-commit.
        transaction->execSqlSync("COMMIT");
        return rowToVerification(updated[0]);
    }

    // Liveness passed: compare against the user's current profile photos.
    auto photoRows = transaction->execSqlSync(
        "SELECT url FROM profile_photos WHERE user_id = $1 ORDER BY position", userId);
    std::vector<std::string> referenceUrls;
    referenceUrls.reserve(photoRows.size());
    for (const auto &row : photoRows)
    {
        referenceUrls.push_back(row["url"].as<std::string>());
    }

    auto faceMatch = provider_->compareFaces(*current.livenessSessionId, referenceUrls);

    const bool passed = livenessResult.confidence.value_or(0.0) >= kLivenessConfidenceThreshold &&
                         faceMatch.bestMatchScore >= kFaceMatchThreshold;

    auto updated = transaction->execSqlSync(
        "UPDATE verifications SET decision = $2, liveness_score = $3, face_match_score = $4, "
        "decided_at = now() WHERE id = $1 "
        "RETURNING id, user_id, decision, liveness_session_id, liveness_score, "
        "face_match_score, submitted_at, decided_at",
        current.id,
        std::string(passed ? "pass" : "fail"),
        livenessResult.confidence,
        faceMatch.bestMatchScore);

    if (passed)
    {
        transaction->execSqlSync("UPDATE profiles SET verified = true WHERE user_id = $1",
                                  userId);
    }

    // Same reasoning as the other explicit commits in this file: this is
    // the write a client immediately follows up with GET /profile/me to
    // observe (that's exactly what test_photo_edit_invalidates_verification.cc
    // and test_http_profile_me.cc do), so it must be durable before we
    // return, not left to an async destructor-triggered commit.
    transaction->execSqlSync("COMMIT");

    return rowToVerification(updated[0]);
}

}  // namespace services

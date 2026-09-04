#include "ReportService.h"

namespace services
{
Json::Value Report::toJson() const
{
    Json::Value j;
    j["id"] = id;
    j["reporter_user_id"] = reporterUserId;
    j["target_type"] = targetType;
    j["target_id"] = targetId;
    j["reason"] = reason;
    j["details_text"] = detailsText;
    j["status"] = status;
    j["created_at"] = createdAt;
    return j;
}

ReportService::ReportService(drogon::orm::DbClientPtr db) : db_(std::move(db))
{
}

std::string ReportService::resolveReportedUserId(const std::string &callerUserId,
                                                   const std::string &targetType,
                                                   const std::string &targetId)
{
    std::string reportedUserId;

    if (targetType == "profile")
    {
        auto rows = db_->execSqlSync("SELECT 1 FROM users WHERE id = $1", targetId);
        if (rows.empty())
        {
            throw NotFoundException("No user " + targetId + ".");
        }
        reportedUserId = targetId;
    }
    else if (targetType == "proposal")
    {
        auto rows =
            db_->execSqlSync("SELECT creator_user_id FROM proposals WHERE id = $1", targetId);
        if (rows.empty())
        {
            throw NotFoundException("No proposal " + targetId + ".");
        }
        reportedUserId = rows[0]["creator_user_id"].as<std::string>();
    }
    else
    {
        // targetType is assumed already field-validated to one of the
        // three known values (ReportValidation), so this branch is
        // "conversation".
        auto rows = db_->execSqlSync(
            "SELECT proposer_user_id, interested_user_id FROM conversations WHERE id = $1",
            targetId);
        if (rows.empty())
        {
            throw NotFoundException("No conversation " + targetId + ".");
        }
        const auto proposerUserId = rows[0]["proposer_user_id"].as<std::string>();
        const auto interestedUserId = rows[0]["interested_user_id"].as<std::string>();

        if (callerUserId == proposerUserId)
        {
            reportedUserId = interestedUserId;
        }
        else if (callerUserId == interestedUserId)
        {
            reportedUserId = proposerUserId;
        }
        else
        {
            throw ReportBadRequestException(
                "NOT_A_PARTICIPANT", "You are not a participant in this conversation.");
        }
    }

    // Checked here -- before createReportRow() or any BlockService call
    // -- so a self-report never creates a report row and never reaches
    // BlockService::createBlock's own (differently-coded) self-block
    // guard.
    if (reportedUserId == callerUserId)
    {
        throw ReportBadRequestException("CANNOT_REPORT_SELF", "You cannot report yourself.");
    }

    return reportedUserId;
}

Report ReportService::createReportRow(const std::string &callerUserId,
                                       const std::string &targetType,
                                       const std::string &targetId,
                                       const std::string &reason,
                                       const std::string &detailsText)
{
    auto rows = db_->execSqlSync(
        "INSERT INTO reports (reporter_user_id, target_type, target_id, reason, details_text) "
        "VALUES ($1, $2, $3, $4, $5) "
        "RETURNING id, reporter_user_id, target_type, target_id, reason, details_text, status, "
        "created_at",
        callerUserId,
        targetType,
        targetId,
        reason,
        detailsText);

    const auto &row = rows[0];
    Report report;
    report.id = row["id"].as<std::string>();
    report.reporterUserId = row["reporter_user_id"].as<std::string>();
    report.targetType = row["target_type"].as<std::string>();
    report.targetId = row["target_id"].as<std::string>();
    report.reason = row["reason"].as<std::string>();
    report.detailsText = row["details_text"].as<std::string>();
    report.status = row["status"].as<std::string>();
    report.createdAt = row["created_at"].as<std::string>();
    return report;
}

}  // namespace services

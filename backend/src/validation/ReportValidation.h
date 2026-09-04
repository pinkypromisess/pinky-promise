#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ProfileValidation.h"  // reused for validation::ValidationError

namespace validation
{
constexpr size_t kMaxReportDetailsTextLength = 2000;

struct ReportCreateInput
{
    std::string targetType;
    std::string targetId;
    std::string reason;
    std::string detailsText;  // may be empty
};

bool isValidReportTargetType(const std::string &targetType);
bool isValidReportReason(const std::string &reason);

// Field-format validation only: target_type/reason enum membership,
// target_id uuid-shape (reuses validation::isLikelyUuid from
// SwipeValidation.h), details_text length. Does NOT resolve target_id to
// a real row or to the reported user -- that's data-dependent and lives
// in ReportService::resolveReportedUserId. Returns every violation found
// (not just the first), same as validateProposalCreate.
std::vector<ValidationError> validateReportCreate(const ReportCreateInput &input);

}  // namespace validation

#pragma once

#include <stdexcept>
#include <string>

namespace services
{
// POST /v1/reports -> 400. `code` distinguishes the cases:
//   NOT_A_PARTICIPANT  -- target_type == "conversation" and the caller
//                         isn't proposer_user_id or interested_user_id on
//                         it, so there's no "other user" to resolve.
//   CANNOT_REPORT_SELF -- the resolved reported-user-id equals the
//                         caller's own id. Thrown from
//                         ReportService::resolveReportedUserId, BEFORE
//                         any report row is inserted or BlockService is
//                         called -- so BlockService::createBlock's own
//                         CANNOT_BLOCK_SELF (a differently-shaped error)
//                         never has a chance to surface through this
//                         Report-shaped error path.
class ReportBadRequestException : public std::runtime_error
{
  public:
    ReportBadRequestException(std::string code, std::string message)
      : std::runtime_error(std::move(message)), code(std::move(code))
    {
    }

    std::string code;
};

}  // namespace services

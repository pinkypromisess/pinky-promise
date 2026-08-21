#pragma once

#include <drogon/HttpFilter.h>

namespace auth
{
// Applies bearerAuth per openapi.yaml: verifies the Authorization header
// and, on success, stores the caller's user id under the "user_id" request
// attribute for controllers to read. Responds 401 (Error schema) otherwise.
class AuthFilter : public drogon::HttpFilter<AuthFilter>
{
  public:
    void doFilter(const drogon::HttpRequestPtr &req,
                  drogon::FilterCallback &&fcb,
                  drogon::FilterChainCallback &&fccb) override;
};

}  // namespace auth

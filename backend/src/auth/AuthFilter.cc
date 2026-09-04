#include "AuthFilter.h"

#include "JwtAuth.h"

namespace auth
{
namespace
{
void respondUnauthorized(drogon::FilterCallback &fcb, const std::string &message)
{
    Json::Value body;
    body["error"] = "UNAUTHORIZED";
    body["message"] = message;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(drogon::k401Unauthorized);
    fcb(resp);
}

}  // namespace

void AuthFilter::doFilter(const drogon::HttpRequestPtr &req,
                          drogon::FilterCallback &&fcb,
                          drogon::FilterChainCallback &&fccb)
{
    const auto authHeader = req->getHeader("Authorization");
    if (authHeader.empty())
    {
        respondUnauthorized(fcb, "Missing Authorization header.");
        return;
    }

    auto userId = verifyAndExtractUserId(authHeader, signingSecret());
    if (!userId)
    {
        respondUnauthorized(fcb, "Invalid or expired bearer token.");
        return;
    }

    req->attributes()->insert("user_id", *userId);
    fccb();
}

}  // namespace auth

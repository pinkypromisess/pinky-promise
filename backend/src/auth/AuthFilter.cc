#include "AuthFilter.h"

#include "JwtAuth.h"

#include <cstdlib>

namespace auth
{
namespace
{
std::string jwtSecret()
{
    if (const char *fromEnv = std::getenv("JWT_SECRET"))
    {
        return fromEnv;
    }
    // Local-dev-only fallback so the server is runnable without extra
    // setup. Cloud Run deployments must set JWT_SECRET.
    return "local-dev-insecure-secret";
}

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

    auto userId = verifyAndExtractUserId(authHeader, jwtSecret());
    if (!userId)
    {
        respondUnauthorized(fcb, "Invalid or expired bearer token.");
        return;
    }

    req->attributes()->insert("user_id", *userId);
    fccb();
}

}  // namespace auth

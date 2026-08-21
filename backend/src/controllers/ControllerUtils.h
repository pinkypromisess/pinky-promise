#pragma once

#include <drogon/HttpResponse.h>

#include <string>
#include <vector>

#include "../validation/ProfileValidation.h"

namespace controllers
{
inline drogon::HttpResponsePtr jsonResponse(const Json::Value &body, drogon::HttpStatusCode status)
{
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(status);
    return resp;
}

inline drogon::HttpResponsePtr errorResponse(drogon::HttpStatusCode status,
                                              const std::string &code,
                                              const std::string &message)
{
    Json::Value body;
    body["error"] = code;
    body["message"] = message;
    return jsonResponse(body, status);
}

// Renders services::ValidationFailedException::errors per the Error
// schema's `details` extension: top-level error/message is the first
// violation, `details` carries all of them.
inline drogon::HttpResponsePtr validationErrorResponse(
    const std::vector<validation::ValidationError> &errors)
{
    Json::Value body;
    body["error"] = errors.front().code;
    body["message"] = errors.front().message;
    Json::Value details(Json::arrayValue);
    for (const auto &e : errors)
    {
        Json::Value d;
        d["error"] = e.code;
        d["message"] = e.message;
        details.append(d);
    }
    body["details"] = details;
    return jsonResponse(body, drogon::k400BadRequest);
}

inline std::string getUserId(const drogon::HttpRequestPtr &req)
{
    return req->attributes()->get<std::string>("user_id");
}

}  // namespace controllers

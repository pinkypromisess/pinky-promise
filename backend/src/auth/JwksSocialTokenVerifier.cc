#include "JwksSocialTokenVerifier.h"

#include <drogon/HttpClient.h>
#include <drogon/HttpTypes.h>
#include <jwt-cpp/jwt.h>

#include <algorithm>
#include <cstdlib>
#include <future>
#include <memory>
#include <stdexcept>
#include <utility>

namespace auth
{
namespace
{
struct SyncHttpResult
{
    drogon::HttpStatusCode status = drogon::kUnknown;
    std::string body;
};

// Blocks the calling thread until the response arrives. `loop` must be a
// loop other than the calling thread's own -- same idiom (and same
// capture-by-value-only reasoning) as
// storage::GcsSignBlobUploadUrlProvider.cc's own syncHttpRequest(): shared
// state is held by shared_ptr and captured BY VALUE into the response
// callback, per CLAUDE.md's flat rule against reference-capturing locals
// or `this` in a callback that outlives the current stack frame.
// doneFuture.wait() below does keep this stack frame alive until the
// callback runs, but the rule is deliberately unconditional, so this is
// safe by construction rather than by argument -- verified by inspection:
// the lambda's capture list is `[state, done]`, both shared_ptrs captured
// by value, nothing else is referenced from the enclosing scope.
SyncHttpResult syncHttpGet(trantor::EventLoop *loop, const std::string &baseUrl,
                            const std::string &path)
{
    auto client = drogon::HttpClient::newHttpClient(baseUrl, loop);
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath(path);

    auto state = std::make_shared<SyncHttpResult>();
    auto done = std::make_shared<std::promise<void>>();
    auto doneFuture = done->get_future();
    client->sendRequest(
        req, [state, done](drogon::ReqResult reqResult, const drogon::HttpResponsePtr &resp) {
            if (reqResult == drogon::ReqResult::Ok && resp != nullptr)
            {
                state->status = resp->statusCode();
                state->body = std::string(resp->body());
            }
            done->set_value();
        });
    doneFuture.wait();

    if (state->status == drogon::kUnknown)
    {
        throw std::runtime_error("request to " + baseUrl + path +
                                  " failed at the transport level");
    }
    return *state;
}

// Splits "https://host/path/to/thing" into ("https://host", "/path/to/thing").
std::pair<std::string, std::string> splitUrl(const std::string &url)
{
    const auto schemeEnd = url.find("://");
    const auto pathStart = url.find('/', schemeEnd == std::string::npos ? 0 : schemeEnd + 3);
    if (pathStart == std::string::npos)
    {
        return {url, "/"};
    }
    return {url.substr(0, pathStart), url.substr(pathStart)};
}

}  // namespace

JwksSocialTokenVerifier::JwksSocialTokenVerifier(std::string providerLabel, std::string jwksUrl,
                                                  std::vector<std::string> acceptableIssuers,
                                                  std::string audienceEnvVar)
  : providerLabel_(std::move(providerLabel)),
    jwksUrl_(std::move(jwksUrl)),
    acceptableIssuers_(std::move(acceptableIssuers)),
    audienceEnvVar_(std::move(audienceEnvVar)),
    ioThread_("social-token-verifier-" + providerLabel_)
{
    ioThread_.run();
}

std::string JwksSocialTokenVerifier::fetchAndCacheJwks()
{
    const auto split = splitUrl(jwksUrl_);
    auto result = syncHttpGet(ioThread_.getLoop(), split.first, split.second);
    if (result.status != drogon::k200OK)
    {
        throw std::runtime_error(providerLabel_ + " JWKS endpoint returned " +
                                  std::to_string(result.status));
    }

    std::lock_guard<std::mutex> lock(jwksMutex_);
    cachedJwksBody_ = result.body;
    return cachedJwksBody_;
}

std::optional<VerifiedSocialIdentity> JwksSocialTokenVerifier::verify(const std::string &idToken)
{
    // Fail closed: there is no safe insecure fallback for an OAuth client
    // id (unlike JWT_SECRET's local-dev default in auth::signingSecret()),
    // so a missing env var must reject rather than silently skip the
    // audience check or crash.
    const char *audienceEnv = std::getenv(audienceEnvVar_.c_str());
    if (audienceEnv == nullptr || std::string(audienceEnv).empty())
    {
        return std::nullopt;
    }
    const std::string audience = audienceEnv;

    try
    {
        // jwt-cpp's own decode()/parse_jwks()/jwk accessors below use its
        // bundled picojson traits internally (jwt::traits::kazuho_picojson,
        // the library's default when JWT_DISABLE_PICOJSON isn't set) --
        // an internal implementation detail confined to this one file.
        // Every value pulled out of them is converted to a plain
        // std::string before this function returns; picojson never
        // reaches any other Module F code.
        auto decoded = jwt::decode(idToken);

        // Explicit algorithm-confusion guard, on top of the one
        // allow_algorithm()/verify() already enforces below (an "alg"
        // this verifier never registered -- e.g. "none" or "HS256" --
        // simply isn't present in its algorithm map, and verify() rejects
        // it outright): belt and braces, and avoids treating an unrelated
        // algorithm's "kid" as meaningful before the JWKS is even
        // consulted.
        if (decoded.get_algorithm() != "RS256")
        {
            return std::nullopt;
        }
        if (!decoded.has_key_id())
        {
            return std::nullopt;
        }
        const auto keyId = decoded.get_key_id();

        // Checked manually rather than via the verifier's with_issuer()
        // (which only supports a single exact match, see jwt-cpp's
        // verifier::with_issuer): Google alone has historically issued
        // tokens with either of two acceptable `iss` values.
        if (!decoded.has_issuer())
        {
            return std::nullopt;
        }
        const auto issuer = decoded.get_issuer();
        if (std::find(acceptableIssuers_.begin(), acceptableIssuers_.end(), issuer) ==
            acceptableIssuers_.end())
        {
            return std::nullopt;
        }

        std::string jwksBody;
        {
            std::lock_guard<std::mutex> lock(jwksMutex_);
            jwksBody = cachedJwksBody_;
        }
        if (jwksBody.empty())
        {
            jwksBody = fetchAndCacheJwks();
        }

        auto jwks = jwt::parse_jwks(jwksBody);
        if (!jwks.has_jwk(keyId))
        {
            // Normal key rotation: re-fetch once before failing, per this
            // module's brief -- simpler than a periodic background timer,
            // appropriate since login isn't a high-frequency job.
            jwksBody = fetchAndCacheJwks();
            jwks = jwt::parse_jwks(jwksBody);
            if (!jwks.has_jwk(keyId))
            {
                return std::nullopt;
            }
        }

        const auto jwk = jwks.get_jwk(keyId);
        const auto modulus = jwk.get_jwk_claim("n").as_string();
        const auto exponent = jwk.get_jwk_claim("e").as_string();
        // Turns the JWK's n/e into a usable PEM public key -- jwt-cpp's
        // own helper, not hand-rolled RSA/base64 handling.
        const auto publicKeyPem =
            jwt::helper::create_public_key_from_rsa_components(modulus, exponent);

        // allow_algorithm(rs256(publicKeyPem)) registers ONLY RS256 under
        // THIS specific key -- verify() below rejects any token whose
        // header "alg" isn't in this verifier's algorithm map, which is
        // jwt-cpp's actual defense against algorithm confusion (an
        // attacker-supplied "alg":"none" or "alg":"HS256" token is simply
        // never looked up). `exp`/`nbf`/`iat` are checked automatically by
        // jwt-cpp's verifier when present, no opt-in call needed.
        auto verifier = jwt::verify()
                             .allow_algorithm(jwt::algorithm::rs256(publicKeyPem))
                             .with_audience(audience)
                             .leeway(60UL);
        verifier.verify(decoded);  // throws on any failure (sig/alg/aud/exp/...)

        VerifiedSocialIdentity identity;
        identity.subject = decoded.get_subject();
        identity.email = decoded.get_payload_claim("email").as_string();
        if (identity.subject.empty() || identity.email.empty())
        {
            return std::nullopt;
        }
        return identity;
    }
    catch (const std::exception &)
    {
        // Any decode/parse/verify failure (bad signature, expired,
        // malformed token, unreachable JWKS, ...) collapses to the same
        // "not verified" outcome -- see SocialTokenVerifier::verify's
        // documented contract; failure subtypes are deliberately not
        // distinguished to the caller.
        return std::nullopt;
    }
}

}  // namespace auth

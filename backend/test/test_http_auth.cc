#include <drogon/drogon_test.h>
#include <drogon/utils/Utilities.h>

#include <string>

#include "TestDb.h"
#include "TestHttpServer.h"
#include "../src/auth/JwtAuth.h"

// Every DROGON_TEST here is HTTP-LEVEL: the request goes through the real
// Drogon router + a real Postgres DB (via the TestHttpServer harness),
// covering Module F.1's POST /v1/auth/signup and POST /v1/auth/login.
//
// Unlike every other HTTP test in this suite, these two routes are
// DELIBERATELY NOT behind auth::AuthFilter (see AuthController.h's
// METHOD_LIST_BEGIN block -- no ADD_METHOD_TO third argument), so "real
// router + real auth filter + real DB" does not apply to them the way it
// does elsewhere: there is no bearer token yet when a client is signing
// up or logging in. That omission is itself part of what several tests
// below assert directly (sending no Authorization header and expecting
// 200/201, not 401).

using namespace test_support;
using namespace drogon;

namespace
{
std::string uniqueTestEmail(const std::string &label)
{
    return "auth-test-" + label + "-" + drogon::utils::getUuid() + "@example.com";
}

Json::Value credentialsBody(const std::string &email, const std::string &password)
{
    Json::Value body;
    body["email"] = email;
    body["password"] = password;
    return body;
}

// True if `value`'s JSON serialization contains `needle` as a substring --
// used to assert a response body never leaks a raw "password" /
// "password_hash" field name or value.
bool jsonContains(const Json::Value &value, const std::string &needle)
{
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    const std::string serialized = Json::writeString(writer, value);
    return serialized.find(needle) != std::string::npos;
}

}  // namespace

// CHECKS: POST /v1/auth/signup with no Authorization header creates a user
// and returns a token that the existing, untouched
// auth::verifyAndExtractUserId (JwtAuth.cc, not modified by this module
// beyond adding signJwt()/signingSecret()) successfully verifies back to
// the same user_id -- direct interop proof, not just "the response has a
// token field". Also covers: the route works with no auth header at all
// (201, not 401), and the response body never leaks "password".
DROGON_TEST(SignupCreatesUserAndReturnsInteroperableToken)
{
    try
    {
        auto baseUrl = ensureTestServerRunning();
        const auto email = uniqueTestEmail("signup");
        auto body = credentialsBody(email, "correct horse battery staple");

        auto resp = sendTestRequest(baseUrl, Post, "/v1/auth/signup", /*bearerToken=*/"", &body);
        REQUIRE(resp.status == k201Created);
        REQUIRE(resp.json.isMember("token"));
        REQUIRE(resp.json.isMember("user_id"));
        const auto userId = resp.json["user_id"].asString();
        CHECK(!userId.empty());

        auto verifiedUserId =
            auth::verifyAndExtractUserId(resp.json["token"].asString(), auth::signingSecret());
        REQUIRE(verifiedUserId.has_value());
        CHECK(*verifiedUserId == userId);

        CHECK(!jsonContains(resp.json, "password"));
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: the password stored by signup is genuinely Argon2id-hashed, not
// plaintext -- read directly from the users.password_hash column via a
// real DB connection (not through any HTTP response).
DROGON_TEST(SignupStoresArgon2idHashNotPlaintext)
{
    try
    {
        auto baseUrl = ensureTestServerRunning();
        auto db = testDbClient();
        const auto email = uniqueTestEmail("hash");
        const std::string password = "correct horse battery staple";
        auto body = credentialsBody(email, password);

        auto resp = sendTestRequest(baseUrl, Post, "/v1/auth/signup", "", &body);
        REQUIRE(resp.status == k201Created);
        const auto userId = resp.json["user_id"].asString();

        auto rows = db->execSqlSync("SELECT password_hash FROM users WHERE id = $1", userId);
        REQUIRE(rows.size() == 1u);
        const auto storedHash = rows[0]["password_hash"].as<std::string>();
        CHECK(storedHash != password);
        CHECK(storedHash.rfind("$argon2id$", 0) == 0);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: a duplicate signup (same email) is rejected 409
// EMAIL_ALREADY_REGISTERED and creates no second users row -- asserted by
// directly querying the row count for that email via the DB, not just
// trusting the HTTP status.
DROGON_TEST(SignupWithDuplicateEmailReturns409AndCreatesNoNewRow)
{
    try
    {
        auto baseUrl = ensureTestServerRunning();
        auto db = testDbClient();
        const auto email = uniqueTestEmail("dup");
        auto body = credentialsBody(email, "correct horse battery staple");

        auto first = sendTestRequest(baseUrl, Post, "/v1/auth/signup", "", &body);
        REQUIRE(first.status == k201Created);

        auto secondBody = credentialsBody(email, "a totally different password");
        auto second = sendTestRequest(baseUrl, Post, "/v1/auth/signup", "", &secondBody);
        CHECK(second.status == k409Conflict);
        CHECK(second.json["error"].asString() == "EMAIL_ALREADY_REGISTERED");

        auto rows = db->execSqlSync("SELECT COUNT(*) AS n FROM users WHERE email = $1", email);
        REQUIRE(rows.size() == 1u);
        CHECK(rows[0]["n"].as<long long>() == 1);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: POST /v1/auth/login with no Authorization header, on a correct
// password, returns 200 with a token/user_id (also covers: this route
// works with no auth header at all).
DROGON_TEST(LoginWithCorrectPasswordReturns200)
{
    try
    {
        auto baseUrl = ensureTestServerRunning();
        const auto email = uniqueTestEmail("login-ok");
        const std::string password = "correct horse battery staple";
        auto signupBody = credentialsBody(email, password);
        auto signupResp = sendTestRequest(baseUrl, Post, "/v1/auth/signup", "", &signupBody);
        REQUIRE(signupResp.status == k201Created);

        auto loginBody = credentialsBody(email, password);
        auto loginResp = sendTestRequest(baseUrl, Post, "/v1/auth/login", "", &loginBody);
        REQUIRE(loginResp.status == k200OK);
        CHECK(loginResp.json["user_id"].asString() == signupResp.json["user_id"].asString());
        CHECK(!jsonContains(loginResp.json, "password"));
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: a token from POST /v1/auth/login actually authenticates a real
// protected endpoint end-to-end -- GET /v1/profile/me with it as a bearer
// token must not be rejected. A 404 ("no profile yet") is the expected
// outcome for a fresh signup; a 401 is exactly the failure this test
// guards against.
DROGON_TEST(LoginTokenAuthenticatesProtectedEndpoint)
{
    try
    {
        auto baseUrl = ensureTestServerRunning();
        const auto email = uniqueTestEmail("login-auth");
        const std::string password = "correct horse battery staple";
        auto signupBody = credentialsBody(email, password);
        auto signupResp = sendTestRequest(baseUrl, Post, "/v1/auth/signup", "", &signupBody);
        REQUIRE(signupResp.status == k201Created);

        auto loginBody = credentialsBody(email, password);
        auto loginResp = sendTestRequest(baseUrl, Post, "/v1/auth/login", "", &loginBody);
        REQUIRE(loginResp.status == k200OK);
        const auto token = loginResp.json["token"].asString();

        auto meResp = sendTestRequest(baseUrl, Get, "/v1/profile/me", token);
        CHECK(meResp.status != k401Unauthorized);
        CHECK((meResp.status == k404NotFound || meResp.status == k200OK));
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: a wrong password and an unknown email both return the exact
// same 401 INVALID_CREDENTIALS code + message -- a client (or an
// attacker probing for valid emails) cannot distinguish the two cases,
// which is the whole point (user-enumeration prevention).
DROGON_TEST(LoginRejectsWrongPasswordAndUnknownEmailIdentically)
{
    try
    {
        auto baseUrl = ensureTestServerRunning();
        const auto email = uniqueTestEmail("login-wrong");
        const std::string password = "correct horse battery staple";
        auto signupBody = credentialsBody(email, password);
        auto signupResp = sendTestRequest(baseUrl, Post, "/v1/auth/signup", "", &signupBody);
        REQUIRE(signupResp.status == k201Created);

        auto wrongPasswordBody = credentialsBody(email, "definitely the wrong password");
        auto wrongPasswordResp =
            sendTestRequest(baseUrl, Post, "/v1/auth/login", "", &wrongPasswordBody);
        REQUIRE(wrongPasswordResp.status == k401Unauthorized);

        auto unknownEmailBody = credentialsBody(uniqueTestEmail("never-signed-up"), password);
        auto unknownEmailResp =
            sendTestRequest(baseUrl, Post, "/v1/auth/login", "", &unknownEmailBody);
        REQUIRE(unknownEmailResp.status == k401Unauthorized);

        CHECK(wrongPasswordResp.json["error"].asString() ==
              unknownEmailResp.json["error"].asString());
        CHECK(wrongPasswordResp.json["message"].asString() ==
              unknownEmailResp.json["message"].asString());
        CHECK(wrongPasswordResp.json["error"].asString() == "INVALID_CREDENTIALS");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: an existing protected route still returns 401 with no
// Authorization header -- spot-checks that this module's AuthFilter.cc /
// signingSecret() refactor (private jwtSecret() -> shared
// auth::signingSecret()) didn't regress anything. (test_http_profile_me.cc
// already covers this same case for its own reasons; asserted again here
// since it's this module's refactor being spot-checked.)
DROGON_TEST(ExistingProtectedRouteStillRejectsNoAuthHeader)
{
    try
    {
        auto baseUrl = ensureTestServerRunning();
        auto resp = sendTestRequest(baseUrl, Get, "/v1/profile/me", /*bearerToken=*/"");
        CHECK(resp.status == k401Unauthorized);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

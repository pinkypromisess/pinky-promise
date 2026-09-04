#include <drogon/drogon_test.h>
#include <drogon/utils/Utilities.h>

#include <string>

#include "TestDb.h"
#include "TestHttpServer.h"
#include "../src/auth/JwtAuth.h"

// Every DROGON_TEST here is HTTP-LEVEL: the request goes through the real
// Drogon router + a real Postgres DB (via the TestHttpServer harness),
// covering Module F.2's POST /v1/auth/social/google and
// POST /v1/auth/social/apple. Like F.1's signup/login, these two routes
// are DELIBERATELY NOT behind auth::AuthFilter -- there is no bearer
// token yet when a client is signing up or logging in via a provider --
// so "real router + real auth filter + real DB" does not apply to them
// the way it does elsewhere.
//
// Token *verification* itself (auth::JwksSocialTokenVerifier -- the real
// jwt-cpp + live-JWKS implementation) is intentionally NOT exercised by
// these tests: the running test server is wired with
// auth::StubSocialTokenVerifier (see TestHttpServer.cc), a deterministic
// fake, so these tests drive AuthService's find-or-create logic and the
// controller's routing/status-code behavior without depending on
// Google's/Apple's real infrastructure or a real signed token.

using namespace test_support;
using namespace drogon;

namespace
{
std::string uniqueSubject(const std::string &label)
{
    return "social-test-" + label + "-" + drogon::utils::getUuid();
}

std::string uniqueEmail(const std::string &label)
{
    return "social-test-" + label + "-" + drogon::utils::getUuid() + "@example.com";
}

Json::Value idTokenBody(const std::string &idToken)
{
    Json::Value body;
    body["id_token"] = idToken;
    return body;
}

long long countAuthIdentities(const drogon::orm::DbClientPtr &db, const std::string &provider,
                               const std::string &subject)
{
    auto rows = db->execSqlSync(
        "SELECT COUNT(*) AS n FROM auth_identities WHERE provider = $1 AND provider_subject = $2",
        provider,
        subject);
    return rows[0]["n"].as<long long>();
}

long long countUsersByEmail(const drogon::orm::DbClientPtr &db, const std::string &email)
{
    auto rows = db->execSqlSync("SELECT COUNT(*) AS n FROM users WHERE email = $1", email);
    return rows[0]["n"].as<long long>();
}

}  // namespace

// CHECKS: POST /v1/auth/social/google with no Authorization header, for a
// brand-new (provider, subject), creates both a users row and an
// auth_identities row and returns 201 with a token that the existing,
// untouched auth::verifyAndExtractUserId (from F.1) successfully verifies
// back to the same user_id -- also covers: the route works with no auth
// header at all.
DROGON_TEST(NewGoogleIdentityCreates201WithInteroperableToken)
{
    try
    {
        auto baseUrl = ensureTestServerRunning();
        auto db = testDbClient();
        auto googleStub = testServerGoogleSocialVerifier();

        const auto subject = uniqueSubject("google-new");
        const auto email = uniqueEmail("google-new");
        auth::VerifiedSocialIdentity identity;
        identity.subject = subject;
        identity.email = email;
        googleStub->setNextIdentity(identity);

        REQUIRE(countAuthIdentities(db, "google", subject) == 0);

        auto body = idTokenBody("irrelevant-stub-consumes-any-string");
        auto resp = sendTestRequest(baseUrl, Post, "/v1/auth/social/google", /*bearerToken=*/"", &body);
        REQUIRE(resp.status == k201Created);
        REQUIRE(resp.json.isMember("token"));
        REQUIRE(resp.json.isMember("user_id"));
        const auto userId = resp.json["user_id"].asString();
        CHECK(!userId.empty());

        auto verifiedUserId =
            auth::verifyAndExtractUserId(resp.json["token"].asString(), auth::signingSecret());
        REQUIRE(verifiedUserId.has_value());
        CHECK(*verifiedUserId == userId);

        CHECK(countAuthIdentities(db, "google", subject) == 1);
        CHECK(countUsersByEmail(db, email) == 1);

        auto userRows = db->execSqlSync("SELECT id FROM users WHERE email = $1", email);
        REQUIRE(userRows.size() == 1u);
        CHECK(userRows[0]["id"].as<std::string>() == userId);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: a repeat social login with the SAME (provider, subject) returns
// 200 (not 201), the same user_id, and creates no duplicate row in either
// users or auth_identities.
DROGON_TEST(RepeatGoogleIdentityReturns200WithNoDuplicateRows)
{
    try
    {
        auto baseUrl = ensureTestServerRunning();
        auto db = testDbClient();
        auto googleStub = testServerGoogleSocialVerifier();

        const auto subject = uniqueSubject("google-repeat");
        const auto email = uniqueEmail("google-repeat");
        auth::VerifiedSocialIdentity identity;
        identity.subject = subject;
        identity.email = email;
        googleStub->setNextIdentity(identity);

        auto body = idTokenBody("token-1");
        auto first = sendTestRequest(baseUrl, Post, "/v1/auth/social/google", "", &body);
        REQUIRE(first.status == k201Created);
        const auto firstUserId = first.json["user_id"].asString();

        // Same identity, as if the client re-authenticated with the
        // provider and got a fresh (but same-subject) id token.
        googleStub->setNextIdentity(identity);
        auto secondBody = idTokenBody("token-2");
        auto second = sendTestRequest(baseUrl, Post, "/v1/auth/social/google", "", &secondBody);
        REQUIRE(second.status == k200OK);
        CHECK(second.json["user_id"].asString() == firstUserId);

        CHECK(countAuthIdentities(db, "google", subject) == 1);
        CHECK(countUsersByEmail(db, email) == 1);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: the SAME email via the OTHER provider (different provider,
// different subject) produces a genuinely separate new user -- proves the
// "no cross-provider account linking" decision (FT-8) is actually
// implemented, not just documented: two distinct user_id values, two
// distinct users rows, each with its own auth_identities row recording
// the shared email via email_at_signup.
//
// users.email itself can hold that shared value on only ONE of the two
// rows -- it carries a pre-existing UNIQUE constraint from migration 001
// (Module A, predates F.2), which a second row with an identical email
// would violate outright (and users_email_or_phone_present rules out
// simply leaving it NULL, since a social account has no phone either).
// AuthService::socialLoginWith() resolves this by giving the SECOND row a
// deterministic, guaranteed-unique placeholder value instead
// ("<subject>@<provider>.social-placeholder.invalid") while still
// recording the true email on BOTH rows' auth_identities.email_at_signup
// (NOT NULL, no uniqueness constraint) -- see the comment in
// AuthService.cc for the full reasoning. That is what this test actually
// asserts: exactly one of the two users rows carries the real shared
// email, the other carries the placeholder, and both auth_identities
// rows carry the real email via email_at_signup regardless.
DROGON_TEST(SameEmailViaOtherProviderCreatesSeparateUser)
{
    try
    {
        auto baseUrl = ensureTestServerRunning();
        auto db = testDbClient();
        auto googleStub = testServerGoogleSocialVerifier();
        auto appleStub = testServerAppleSocialVerifier();

        const auto sharedEmail = uniqueEmail("shared");
        const auto googleSubject = uniqueSubject("cross-google");
        const auto appleSubject = uniqueSubject("cross-apple");

        auth::VerifiedSocialIdentity googleIdentity;
        googleIdentity.subject = googleSubject;
        googleIdentity.email = sharedEmail;
        googleStub->setNextIdentity(googleIdentity);
        auto googleBody = idTokenBody("google-token");
        auto googleResp =
            sendTestRequest(baseUrl, Post, "/v1/auth/social/google", "", &googleBody);
        REQUIRE(googleResp.status == k201Created);
        const auto googleUserId = googleResp.json["user_id"].asString();

        auth::VerifiedSocialIdentity appleIdentity;
        appleIdentity.subject = appleSubject;
        appleIdentity.email = sharedEmail;
        appleStub->setNextIdentity(appleIdentity);
        auto appleBody = idTokenBody("apple-token");
        auto appleResp = sendTestRequest(baseUrl, Post, "/v1/auth/social/apple", "", &appleBody);
        REQUIRE(appleResp.status == k201Created);
        const auto appleUserId = appleResp.json["user_id"].asString();

        // Two genuinely separate users -- the actual FT-8 assertion.
        CHECK(googleUserId != appleUserId);
        CHECK(countAuthIdentities(db, "google", googleSubject) == 1);
        CHECK(countAuthIdentities(db, "apple", appleSubject) == 1);

        // Exactly one of the two rows carries the shared email in
        // users.email (the UNIQUE constraint means it can't be both);
        // both auth_identities rows still record it via email_at_signup.
        CHECK(countUsersByEmail(db, sharedEmail) == 1);
        auto identityEmails = db->execSqlSync(
            "SELECT email_at_signup FROM auth_identities WHERE provider_subject IN ($1, $2)",
            googleSubject,
            appleSubject);
        REQUIRE(identityEmails.size() == 2u);
        for (const auto &row : identityEmails)
        {
            CHECK(row["email_at_signup"].as<std::string>() == sharedEmail);
        }

        // The two users rows themselves: one has the real shared email,
        // the other has the deterministic ".invalid" placeholder --
        // never the same value on both (that's what the UNIQUE
        // constraint would otherwise reject), and never NULL on either
        // (that's what users_email_or_phone_present would reject).
        auto userEmails = db->execSqlSync(
            "SELECT email FROM users WHERE id IN ($1, $2)", googleUserId, appleUserId);
        REQUIRE(userEmails.size() == 2u);
        int realEmailCount = 0;
        int placeholderCount = 0;
        for (const auto &row : userEmails)
        {
            REQUIRE(!row["email"].isNull());
            const auto value = row["email"].as<std::string>();
            if (value == sharedEmail)
            {
                ++realEmailCount;
            }
            else if (value.find(".social-placeholder.invalid") != std::string::npos)
            {
                ++placeholderCount;
            }
        }
        CHECK(realEmailCount == 1);
        CHECK(placeholderCount == 1);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: a stub configured to fail verification (simulating a bad
// signature / expired / wrong-audience token) returns 401
// INVALID_SOCIAL_TOKEN and creates nothing in either users or
// auth_identities.
DROGON_TEST(FailedVerificationReturns401AndCreatesNothing)
{
    try
    {
        auto baseUrl = ensureTestServerRunning();
        auto db = testDbClient();
        auto appleStub = testServerAppleSocialVerifier();
        appleStub->setNextFailure();

        auto usersBefore = db->execSqlSync("SELECT COUNT(*) AS n FROM users");
        auto identitiesBefore = db->execSqlSync("SELECT COUNT(*) AS n FROM auth_identities");
        const auto usersBeforeCount = usersBefore[0]["n"].as<long long>();
        const auto identitiesBeforeCount = identitiesBefore[0]["n"].as<long long>();

        auto body = idTokenBody("bad-token");
        auto resp = sendTestRequest(baseUrl, Post, "/v1/auth/social/apple", "", &body);
        REQUIRE(resp.status == k401Unauthorized);
        CHECK(resp.json["error"].asString() == "INVALID_SOCIAL_TOKEN");

        auto usersAfter = db->execSqlSync("SELECT COUNT(*) AS n FROM users");
        auto identitiesAfter = db->execSqlSync("SELECT COUNT(*) AS n FROM auth_identities");
        CHECK(usersAfter[0]["n"].as<long long>() == usersBeforeCount);
        CHECK(identitiesAfter[0]["n"].as<long long>() == identitiesBeforeCount);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: POST /v1/auth/social/apple, independently of Google (separate
// case from NewGoogleIdentityCreates201WithInteroperableToken above),
// also creates a user + identity and returns an interoperable token, with
// no Authorization header required.
DROGON_TEST(NewAppleIdentityCreates201WithInteroperableToken)
{
    try
    {
        auto baseUrl = ensureTestServerRunning();
        auto db = testDbClient();
        auto appleStub = testServerAppleSocialVerifier();

        const auto subject = uniqueSubject("apple-new");
        const auto email = uniqueEmail("apple-new");
        auth::VerifiedSocialIdentity identity;
        identity.subject = subject;
        identity.email = email;
        appleStub->setNextIdentity(identity);

        auto body = idTokenBody("apple-id-token");
        auto resp = sendTestRequest(baseUrl, Post, "/v1/auth/social/apple", "", &body);
        REQUIRE(resp.status == k201Created);
        const auto userId = resp.json["user_id"].asString();

        auto verifiedUserId =
            auth::verifyAndExtractUserId(resp.json["token"].asString(), auth::signingSecret());
        REQUIRE(verifiedUserId.has_value());
        CHECK(*verifiedUserId == userId);

        CHECK(countAuthIdentities(db, "apple", subject) == 1);
        CHECK(countUsersByEmail(db, email) == 1);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

// CHECKS: a social-only signup leaves users.password_hash NULL (read
// directly from the DB), and POST /v1/auth/login for that same email
// correctly falls into F.1's existing "no password_hash ->
// INVALID_CREDENTIALS" path rather than crashing -- this exercises F.1
// code but proves F.2 didn't break the NULL-password_hash case F.2 itself
// introduces.
DROGON_TEST(SocialOnlyUserHasNullPasswordHashAndLoginRejectsCleanly)
{
    try
    {
        auto baseUrl = ensureTestServerRunning();
        auto db = testDbClient();
        auto googleStub = testServerGoogleSocialVerifier();

        const auto subject = uniqueSubject("google-nullpw");
        const auto email = uniqueEmail("google-nullpw");
        auth::VerifiedSocialIdentity identity;
        identity.subject = subject;
        identity.email = email;
        googleStub->setNextIdentity(identity);

        auto signupBody = idTokenBody("google-id-token");
        auto signupResp =
            sendTestRequest(baseUrl, Post, "/v1/auth/social/google", "", &signupBody);
        REQUIRE(signupResp.status == k201Created);
        const auto userId = signupResp.json["user_id"].asString();

        auto rows = db->execSqlSync("SELECT password_hash FROM users WHERE id = $1", userId);
        REQUIRE(rows.size() == 1u);
        CHECK(rows[0]["password_hash"].isNull());

        Json::Value loginBody;
        loginBody["email"] = email;
        loginBody["password"] = "some-password-this-account-never-set";
        auto loginResp = sendTestRequest(baseUrl, Post, "/v1/auth/login", "", &loginBody);
        REQUIRE(loginResp.status == k401Unauthorized);
        CHECK(loginResp.json["error"].asString() == "INVALID_CREDENTIALS");
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("HTTP test setup or request failed: ") + e.what());
    }
}

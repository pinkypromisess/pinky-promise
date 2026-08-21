#pragma once

#include <drogon/orm/DbClient.h>

#include <string>
#include <vector>

#include "../src/validation/ProfileValidation.h"

// Shared setup for tests that need a real Postgres, as opposed to testing
// validation::ProfileValidation or StubFaceVerificationProvider in
// isolation. Reads the same DB_HOST/DB_PORT/DB_NAME/DB_USER/DB_PASSWORD
// env vars as src/main.cc and the CI workflow, so it works against
// whatever Postgres the migrations were already applied to (locally or in
// CI) without any extra test-specific config.
namespace test_support
{
// Lazily creates (once per process) a standalone Postgres client — no
// drogon::app() event loop required for this, see
// drogon::orm::DbClient::newPgClient. Throws if the connection can't be
// established; callers should wrap use of this in try/catch and FAIL()
// the test with a clear message rather than let an exception escape
// uncaught (drogon's test framework has no top-level exception handler
// around a DROGON_TEST body, so an uncaught exception crashes the whole
// test process instead of just failing one test).
drogon::orm::DbClientPtr testDbClient();

// Inserts a fresh `users` row with a random id/email and returns the id,
// so DB-dependent tests get their own isolated user instead of colliding
// with rows left behind by other test runs.
std::string createTestUser(const drogon::orm::DbClientPtr &db);

// `count` distinct photo URLs, e.g. sixPhotoUrls("p") -> {"p1", ..., "p6"}.
std::vector<std::string> sixPhotoUrls(const std::string &prefix = "photo");

// A ProfileUpsertInput that passes validation, using the given photo URLs.
validation::ProfileUpsertInput makeTestProfileInput(std::vector<std::string> photoUrls);

}  // namespace test_support

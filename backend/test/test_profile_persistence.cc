#include <drogon/drogon_test.h>

#include <memory>

#include "TestDb.h"
#include "../src/services/ProfileService.h"
#include "../src/services/VerificationService.h"
#include "../src/verification/StubFaceVerificationProvider.h"

using namespace services;

// CHECKS: PUT /profile creates exactly one row on the first call, and updates that same row (not a duplicate) on a second call
DROGON_TEST(SecondUpsertUpdatesSameRowNotADuplicate)
{
    drogon::orm::DbClientPtr db;
    std::string userId;
    try
    {
        db = test_support::testDbClient();
        userId = test_support::createTestUser(db);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("could not reach the test database: ") + e.what());
        return;
    }

    ProfileService profileService(db);
    auto photos = test_support::sixPhotoUrls("row");
    auto created = profileService.upsertProfile(userId, test_support::makeTestProfileInput(photos));
    CHECK(created.name == "Test User");

    auto countAfterCreate =
        db->execSqlSync("SELECT count(*) AS c FROM profiles WHERE user_id = $1", userId);
    REQUIRE(countAfterCreate[0]["c"].as<int64_t>() == 1);

    auto secondInput = test_support::makeTestProfileInput(photos);
    secondInput.name = "Updated Name";
    auto updated = profileService.upsertProfile(userId, secondInput);
    CHECK(updated.userId == created.userId);
    CHECK(updated.name == "Updated Name");
    CHECK(updated.createdAt == created.createdAt);  // same row, not a freshly created one

    auto countAfterUpdate =
        db->execSqlSync("SELECT count(*) AS c FROM profiles WHERE user_id = $1", userId);
    CHECK(countAfterUpdate[0]["c"].as<int64_t>() == 1);
}

// CHECKS: submitting the identical photo_urls list on a second PUT leaves verified unchanged, instead of unconditionally resetting it like a genuine photo change does
DROGON_TEST(IdenticalPhotoSetOnSecondUpsertLeavesVerifiedUnchanged)
{
    drogon::orm::DbClientPtr db;
    std::string userId;
    try
    {
        db = test_support::testDbClient();
        userId = test_support::createTestUser(db);
    }
    catch (const std::exception &e)
    {
        FAIL(std::string("could not reach the test database: ") + e.what());
        return;
    }

    ProfileService profileService(db);
    auto photos = test_support::sixPhotoUrls("same");
    profileService.upsertProfile(userId, test_support::makeTestProfileInput(photos));

    auto provider = std::make_shared<verification::StubFaceVerificationProvider>();
    VerificationService verificationService(db, provider);
    verificationService.startVerification(userId);
    verificationService.getStatus(userId);
    REQUIRE(profileService.getProfile(userId).verified);

    // Same photo list, only the name changes.
    auto secondInput = test_support::makeTestProfileInput(photos);
    secondInput.name = "Still Verified";
    auto updated = profileService.upsertProfile(userId, secondInput);
    CHECK(updated.verified);
    CHECK(profileService.getProfile(userId).verified);
}

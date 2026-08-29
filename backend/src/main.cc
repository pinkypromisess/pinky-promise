#include <drogon/drogon.h>

#include <cstdlib>
#include <string>

#include "AppContext.h"
#include "storage/StubGcsUploadUrlProvider.h"
#include "verification/StubFaceVerificationProvider.h"

namespace
{
std::string envOr(const char *name, const std::string &fallback)
{
    if (const char *value = std::getenv(name))
    {
        return value;
    }
    return fallback;
}

// Builds the Postgres connection config entirely from the environment
// rather than config.json, since Cloud Run/Cloud SQL credentials and the
// instance connection name aren't known until deploy time. `DB_HOST` may
// be a TCP host or a Cloud SQL unix socket directory (e.g.
// "/cloudsql/<project>:<region>:<instance>") — libpq treats a host
// starting with '/' as a unix socket path automatically.
drogon::orm::PostgresConfig buildDbConfig()
{
    drogon::orm::PostgresConfig config;
    config.host = envOr("DB_HOST", "localhost");
    config.port = static_cast<unsigned short>(std::stoi(envOr("DB_PORT", "5432")));
    config.databaseName = envOr("DB_NAME", "pinky_promise");
    config.username = envOr("DB_USER", "postgres");
    config.password = envOr("DB_PASSWORD", "");
    config.connectionNumber = static_cast<size_t>(std::stoi(envOr("DB_POOL_SIZE", "4")));
    config.name = "default";
    // This module relies on execSqlSync (see ProfileService/VerificationService),
    // which only works on a non-fast client.
    config.isFast = false;
    config.characterSet = "utf8";
    config.timeout = -1.0;
    config.autoBatch = false;
    return config;
}

}  // namespace

int main(int argc, char *argv[])
{
    const std::string configFile = argc > 1 ? argv[1] : "config/config.json";
    drogon::app().loadConfigFile(configFile);

    // Cloud Run injects PORT and expects the container to listen on it.
    const auto port = static_cast<unsigned short>(std::stoi(envOr("PORT", "8080")));
    drogon::app().addListener("0.0.0.0", port);

    drogon::app().addDbClient(buildDbConfig());

    // Runs once DB clients are up but before the server starts accepting
    // connections — the right place to wire up services that need the pool.
    drogon::app().registerBeginningAdvice([]() {
        // TODO: swap for verification::RekognitionFaceVerificationProvider
        // once AWS credentials are provisioned for this environment. The
        // stub keeps the app runnable end-to-end without them.
        auto faceProvider = std::make_shared<verification::StubFaceVerificationProvider>();
        // TODO: swap for storage::GcsSignBlobUploadUrlProvider (already
        // implemented, see src/storage/) once deployed to Cloud Run —
        // nothing in local dev/CI can reach the metadata server or IAM
        // API it depends on.
        auto photoUploadProvider = std::make_shared<storage::StubGcsUploadUrlProvider>();
        app_context::init(drogon::app().getDbClient(), faceProvider, photoUploadProvider);
        LOG_INFO << "Module A (Profile & Verification) initialized.";
    });

    drogon::app().run();
    return 0;
}

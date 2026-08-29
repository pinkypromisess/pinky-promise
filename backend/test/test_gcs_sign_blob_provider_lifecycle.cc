#include <drogon/drogon_test.h>

#include "../src/storage/GcsSignBlobUploadUrlProvider.h"

// GcsSignBlobUploadUrlProvider's actual work — calling the Cloud Run
// metadata server and the IAM Credentials API's signBlob method — needs a
// real GCP environment nothing in this repo has, so it cannot be
// exercised here; that's covered by test_gcs_v4_signing.cc's pure-logic
// tests of the deterministic parts instead, and needs manual verification
// on a real deployment before this provider is ever wired up as the
// default (see the TODO in src/main.cc). This test only confirms
// construction/destruction — starting and cleanly stopping the dedicated
// background event loop thread it owns — doesn't hang or crash. No
// network call happens here.

using namespace storage;

// CHECKS: constructing and destroying the provider (which starts/stops its own background event loop thread) completes cleanly, with no network I/O
DROGON_TEST(ConstructsAndDestructsCleanly)
{
    {
        GcsSignBlobUploadUrlProvider provider("some-bucket");
        (void)provider;
    }
    SUCCESS();
}

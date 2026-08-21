#define DROGON_TEST_MAIN
#include <drogon/drogon_test.h>

// All of this module's tests are plain synchronous C++ (validation logic,
// the stub verification provider) — no HTTP server or DB connection is
// needed, so unlike Drogon's own examples we don't need to spin up the
// event loop here.
int main(int argc, char **argv)
{
    return drogon::test::run(argc, argv);
}

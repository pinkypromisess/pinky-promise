# Reuses the exact same sources as the server executable (see the note in
# the top-level CMakeLists.txt about why these can't sit in a static lib).
add_executable(pinky_promise_tests
    test_main.cc
    test_profile_validation.cc
    test_verification_stub.cc
    TestDb.cc
    TestHttpServer.cc
    TestHttpFixtures.cc
    test_photo_edit_invalidates_verification.cc
    test_verification_thresholds.cc
    test_verification_gate.cc
    test_profile_persistence.cc
    test_http_profile_me.cc
    test_proposal_validation.cc
    test_http_proposals.cc
    test_photo_upload_validation.cc
    test_gcs_v4_signing.cc
    test_gcs_sign_blob_provider_lifecycle.cc
    test_photo_upload_url.cc
    test_swipe_validation.cc
    test_http_swipe.cc
    test_conversation_expiry.cc
    test_http_conversations.cc
    test_http_pinky_promise.cc
    test_http_proposal_cascade.cc
    ${CORE_SOURCES}
)
target_include_directories(pinky_promise_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pinky_promise_tests PRIVATE Drogon::Drogon OpenSSL::Crypto)

include(ParseAndAddDrogonTests)
ParseAndAddDrogonTests(pinky_promise_tests)

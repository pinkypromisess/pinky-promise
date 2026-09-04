cmake_minimum_required(VERSION 3.16)
project(pinky_promise_backend CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Drogon CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

# Controllers and the auth filter self-register with Drogon via static
# initializers (DrObject reflection), not through symbols anything else
# calls. If these were built into a plain static library, the linker would
# see nothing referencing them and silently drop those object files, so
# the routes would never register (that isn't hypothetical — it's exactly
# what happened here first). Listing the sources directly on each
# executable target instead guarantees every translation unit is linked
# in, at the cost of compiling them twice (once per target).
set(CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/AppContext.cc
    ${CMAKE_SOURCE_DIR}/src/auth/JwtAuth.cc
    ${CMAKE_SOURCE_DIR}/src/auth/AuthFilter.cc
    ${CMAKE_SOURCE_DIR}/src/validation/ProfileValidation.cc
    ${CMAKE_SOURCE_DIR}/src/validation/ProposalValidation.cc
    ${CMAKE_SOURCE_DIR}/src/validation/PhotoUploadValidation.cc
    ${CMAKE_SOURCE_DIR}/src/validation/SwipeValidation.cc
    ${CMAKE_SOURCE_DIR}/src/validation/MessageValidation.cc
    ${CMAKE_SOURCE_DIR}/src/verification/StubFaceVerificationProvider.cc
    ${CMAKE_SOURCE_DIR}/src/notifications/StubReminderProvider.cc
    ${CMAKE_SOURCE_DIR}/src/storage/TimeUtils.cc
    ${CMAKE_SOURCE_DIR}/src/storage/GcsV4Signing.cc
    ${CMAKE_SOURCE_DIR}/src/storage/StubGcsUploadUrlProvider.cc
    ${CMAKE_SOURCE_DIR}/src/storage/GcsSignBlobUploadUrlProvider.cc
    ${CMAKE_SOURCE_DIR}/src/services/ProfileService.cc
    ${CMAKE_SOURCE_DIR}/src/services/VerificationService.cc
    ${CMAKE_SOURCE_DIR}/src/services/ProposalService.cc
    ${CMAKE_SOURCE_DIR}/src/services/PhotoUploadService.cc
    ${CMAKE_SOURCE_DIR}/src/services/SwipeService.cc
    ${CMAKE_SOURCE_DIR}/src/services/ConversationExpiry.cc
    ${CMAKE_SOURCE_DIR}/src/services/ConversationService.cc
    ${CMAKE_SOURCE_DIR}/src/services/PinkyPromiseService.cc
    ${CMAKE_SOURCE_DIR}/src/services/ReminderService.cc
    ${CMAKE_SOURCE_DIR}/src/services/ReminderSweepScheduler.cc
    ${CMAKE_SOURCE_DIR}/src/services/BlockService.cc
    ${CMAKE_SOURCE_DIR}/src/controllers/ProfileController.cc
    ${CMAKE_SOURCE_DIR}/src/controllers/VerificationController.cc
    ${CMAKE_SOURCE_DIR}/src/controllers/ProposalController.cc
    ${CMAKE_SOURCE_DIR}/src/controllers/SwipeController.cc
    ${CMAKE_SOURCE_DIR}/src/controllers/ConversationController.cc
    ${CMAKE_SOURCE_DIR}/src/controllers/PinkyPromiseController.cc
    ${CMAKE_SOURCE_DIR}/src/controllers/BlockController.cc
)

add_executable(pinky_promise_backend src/main.cc ${CORE_SOURCES})
target_include_directories(pinky_promise_backend PRIVATE src)
target_link_libraries(pinky_promise_backend PRIVATE Drogon::Drogon OpenSSL::Crypto)

enable_testing()
add_subdirectory(test)

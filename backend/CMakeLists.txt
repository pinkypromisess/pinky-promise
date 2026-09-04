cmake_minimum_required(VERSION 3.16)
project(pinky_promise_backend CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Drogon CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

# Argon2id password hashing (Module F.1) via the actual Password Hashing
# Competition reference implementation, apt package libargon2-dev
# (provides <argon2.h> / libargon2) -- not OpenSSL primitives, not
# hand-rolled. libargon2-dev ships a libargon2.pc pkg-config file, so try
# that first; fall back to a manual find_library/find_path if pkg-config
# isn't available (e.g. some non-Debian dev setups).
find_package(PkgConfig REQUIRED)
pkg_check_modules(ARGON2 IMPORTED_TARGET libargon2)
if(ARGON2_FOUND)
    set(ARGON2_TARGET PkgConfig::ARGON2)
else()
    find_library(ARGON2_LIBRARY NAMES argon2)
    find_path(ARGON2_INCLUDE_DIR NAMES argon2.h)
    if(NOT ARGON2_LIBRARY OR NOT ARGON2_INCLUDE_DIR)
        message(FATAL_ERROR "libargon2 not found (tried pkg-config libargon2, then find_library/find_path). Install libargon2-dev.")
    endif()
    add_library(argon2_fallback UNKNOWN IMPORTED)
    set_target_properties(argon2_fallback PROPERTIES
        IMPORTED_LOCATION ${ARGON2_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${ARGON2_INCLUDE_DIR})
    set(ARGON2_TARGET argon2_fallback)
endif()

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
    ${CMAKE_SOURCE_DIR}/src/validation/AuthValidation.cc
    ${CMAKE_SOURCE_DIR}/src/validation/ProfileValidation.cc
    ${CMAKE_SOURCE_DIR}/src/validation/ProposalValidation.cc
    ${CMAKE_SOURCE_DIR}/src/validation/PhotoUploadValidation.cc
    ${CMAKE_SOURCE_DIR}/src/validation/SwipeValidation.cc
    ${CMAKE_SOURCE_DIR}/src/validation/MessageValidation.cc
    ${CMAKE_SOURCE_DIR}/src/validation/ReportValidation.cc
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
    ${CMAKE_SOURCE_DIR}/src/services/ReportService.cc
    ${CMAKE_SOURCE_DIR}/src/services/AuthService.cc
    ${CMAKE_SOURCE_DIR}/src/controllers/AuthController.cc
    ${CMAKE_SOURCE_DIR}/src/controllers/ProfileController.cc
    ${CMAKE_SOURCE_DIR}/src/controllers/VerificationController.cc
    ${CMAKE_SOURCE_DIR}/src/controllers/ProposalController.cc
    ${CMAKE_SOURCE_DIR}/src/controllers/SwipeController.cc
    ${CMAKE_SOURCE_DIR}/src/controllers/ConversationController.cc
    ${CMAKE_SOURCE_DIR}/src/controllers/PinkyPromiseController.cc
    ${CMAKE_SOURCE_DIR}/src/controllers/BlockController.cc
    ${CMAKE_SOURCE_DIR}/src/controllers/ReportController.cc
)

add_executable(pinky_promise_backend src/main.cc ${CORE_SOURCES})
target_include_directories(pinky_promise_backend PRIVATE src)
target_link_libraries(pinky_promise_backend PRIVATE Drogon::Drogon OpenSSL::Crypto ${ARGON2_TARGET})

enable_testing()
add_subdirectory(test)

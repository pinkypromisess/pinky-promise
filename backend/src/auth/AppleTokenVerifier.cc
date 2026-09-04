#include "AppleTokenVerifier.h"

#include <vector>

#include "JwksSocialTokenVerifier.h"

namespace auth
{
std::shared_ptr<SocialTokenVerifier> makeAppleTokenVerifier()
{
    return std::make_shared<JwksSocialTokenVerifier>("apple",
                                                       "https://appleid.apple.com/auth/keys",
                                                       std::vector<std::string>{
                                                           "https://appleid.apple.com"},
                                                       "APPLE_SERVICES_ID");
}

}  // namespace auth

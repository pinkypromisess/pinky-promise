#include "GoogleTokenVerifier.h"

#include <vector>

#include "JwksSocialTokenVerifier.h"

namespace auth
{
std::shared_ptr<SocialTokenVerifier> makeGoogleTokenVerifier()
{
    return std::make_shared<JwksSocialTokenVerifier>(
        "google",
        "https://www.googleapis.com/oauth2/v3/certs",
        std::vector<std::string>{"https://accounts.google.com", "accounts.google.com"},
        "GOOGLE_OAUTH_CLIENT_ID");
}

}  // namespace auth

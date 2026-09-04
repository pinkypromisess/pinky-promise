#include "AuthValidation.h"

namespace validation
{
bool isValidEmailShape(const std::string &email)
{
    const auto at = email.find('@');
    if (at == std::string::npos || at == 0)
    {
        return false;  // no '@', or empty local part
    }
    if (email.find('@', at + 1) != std::string::npos)
    {
        return false;  // more than one '@'
    }

    const std::string domain = email.substr(at + 1);
    if (domain.empty())
    {
        return false;
    }
    const auto dot = domain.find('.');
    if (dot == std::string::npos || dot == 0 || dot == domain.size() - 1)
    {
        return false;  // no '.', or it's the domain's first/last character
    }

    return true;
}

bool isValidPasswordLength(const std::string &password)
{
    return password.size() >= kMinPasswordLength;
}

}  // namespace validation

#pragma once

#include <cstddef>
#include <string>

namespace validation
{
// OWASP/NIST current guidance favors length over composition rules for an
// MVP like this one -- no forced uppercase/digit/symbol.
constexpr size_t kMinPasswordLength = 8;

// Simple sanity check, deliberately not full RFC 5322: non-empty local
// part, exactly one '@', and a domain containing a '.' that isn't the
// domain's first or last character (so "a@b." / "a@.b" are rejected too).
bool isValidEmailShape(const std::string &email);

bool isValidPasswordLength(const std::string &password);

}  // namespace validation

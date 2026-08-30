#pragma once

#include <optional>
#include <string>

#include "ProfileValidation.h"  // reused for validation::ValidationError

namespace validation
{
constexpr size_t kMaxMessageContentLength = 4000;

// The only two message types (mirrors the CHECK constraint in
// migrations/007_create_conversations_messages.sql).
bool isValidMessageType(const std::string &type);

// Field-format validation for POST /v1/conversations/{id}/messages'
// body: `type` present and one of {text, voice}; `content` present,
// non-blank, within length. Returns std::nullopt when well-formed.
std::optional<ValidationError> validateMessage(const std::string &type,
                                                const std::string &content);

}  // namespace validation

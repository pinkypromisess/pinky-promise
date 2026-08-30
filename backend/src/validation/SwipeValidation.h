#pragma once

#include <optional>
#include <string>

#include "ProfileValidation.h"  // reused for validation::ValidationError

namespace validation
{
// The only two swipe actions (mirrors the CHECK constraint in
// migrations/006_create_swipes.sql).
bool isValidSwipeAction(const std::string &action);

// Loose 8-4-4-4-12 hex-and-dash check for the {id} path segment, so a
// clearly-malformed proposal id becomes a clean 404 instead of a Postgres
// "invalid input syntax for type uuid" error bubbling out of the service.
bool isLikelyUuid(const std::string &value);

// Field-format validation for POST /proposals/{id}/swipe's body: `action`
// present and one of the two allowed values. Returns std::nullopt when the
// input is well-formed.
std::optional<ValidationError> validateSwipeAction(const std::string &action);

}  // namespace validation

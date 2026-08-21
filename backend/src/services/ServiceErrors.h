#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "../validation/ProfileValidation.h"

namespace services
{
// Maps to a 400 response carrying one or more field-level errors.
class ValidationFailedException : public std::runtime_error
{
  public:
    explicit ValidationFailedException(std::vector<validation::ValidationError> errors)
      : std::runtime_error("validation failed"), errors(std::move(errors))
    {
    }

    std::vector<validation::ValidationError> errors;
};

// Maps to a 404 response.
class NotFoundException : public std::runtime_error
{
  public:
    explicit NotFoundException(std::string message) : std::runtime_error(std::move(message))
    {
    }
};

}  // namespace services

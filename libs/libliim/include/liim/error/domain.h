#pragma once

#include <liim/error/transport.h>
#include <liim/forward.h>

namespace LIIM::Error {
class ErrorDomain {
public:
    constexpr ErrorDomain() = default;
    constexpr virtual ~ErrorDomain() {}

    virtual void destroy_error(ErrorTransport<>& value) const = 0;
    virtual ErasedString message(const ErrorTransport<>& value) const = 0;
    virtual ErasedString type() const = 0;
};
}

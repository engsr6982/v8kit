#pragma once
#include "v8kit/Macro.h"
#include <exception>
#include <memory>
#include <string>


V8KIT_WARNING_GUARD_BEGIN
#include <v8-exception.h>
#include <v8-persistent-handle.h>
#include <v8-value.h>
V8KIT_WARNING_GUARD_END


namespace v8kit {


class Exception final : public std::exception {
public:
    enum class Type {
        Unknown = -1, // JavaScript 侧抛出的异常为 Unknown
        Error,
        RangeError,
        ReferenceError,
        SyntaxError,
        TypeError
    };

    explicit Exception(v8::TryCatch const& tryCatch);
    explicit Exception(std::string message, Type type = Type::Error);

    // The C++ standard requires exception classes to be reproducible
    Exception(Exception const&)                = default;
    Exception& operator=(Exception const&)     = default;
    Exception(Exception&&) noexcept            = default;
    Exception& operator=(Exception&&) noexcept = default;

    [[nodiscard]] Type type() const noexcept;

    [[nodiscard]] char const* what() const noexcept override;

    [[nodiscard]] std::string message() const noexcept;

    [[nodiscard]] std::string stacktrace() const noexcept;

    /**
     * Throw this exception to v8 (JavaScript).
     * Normally we don't need to call this method, the package library handles exceptions internally.
     * You just need to re-throw 'throw e;`
     * Or throw Exception inside the Function callback.
     */
    void rethrowToRuntime() const;

public:
    /**
     * Re-throw the exception in v8::TryCatch as a Exception
     */
    static void rethrow(v8::TryCatch const& tryCatch);

private:
    void extractMessage() const noexcept;
    void makeException() const;

    /**
     * v8::Global does not allow copying of resources,
     * but the C++ standard requires exception classes to be replicated
     */
    struct ExceptionContext {
        Type                          type{Type::Unknown};
        mutable std::string           message{};
        mutable v8::Global<v8::Value> exception{};
    };

    std::shared_ptr<ExceptionContext> ctx_{nullptr};
};


} // namespace v8kit
#include "Exception.h"

#include "EngineScope.h"
#include "Reference.h"
#include "Value.h"


#include <algorithm>
#include <exception>


V8KIT_WARNING_GUARD_BEGIN
#include <v8-exception.h>
#include <v8-local-handle.h>
#include <v8-persistent-handle.h>
#include <v8-primitive.h>
#include <v8-value.h>
#include <v8.h>
V8KIT_WARNING_GUARD_END

namespace v8kit {


Exception::Exception(v8::TryCatch const& tryCatch)
: std::exception(),
  ctx_(std::make_shared<ExceptionContext>()) {
    auto isolate = EngineScope::currentRuntimeIsolateChecked();

    ctx_->exception = v8::Global<v8::Value>(isolate, tryCatch.Exception());
}

Exception::Exception(std::string message, Type type)
: std::exception(),
  ctx_(std::make_shared<ExceptionContext>()) {
    ctx_->type    = type;
    ctx_->message = std::move(message);
    makeException(); // null exception, make it
}


Exception::Type Exception::type() const noexcept { return ctx_->type; }

char const* Exception::what() const noexcept {
    extractMessage();
    return ctx_->message.c_str();
}

std::string Exception::message() const noexcept {
    extractMessage();
    return ctx_->message;
}

std::string Exception::stacktrace() const noexcept {
    auto&& [isolate, ctx] = EngineScope::currentIsolateAndContextChecked();

    auto vtry = v8::TryCatch{isolate}; // noexcept

    auto stack = v8::TryCatch::StackTrace(ctx, ctx_->exception.Get(isolate));
    if (!stack.IsEmpty()) {
        v8::String::Utf8Value ut8{isolate, stack.ToLocalChecked()};
        if (auto str = *ut8) {
            return str;
        }
    }
    return "[ERROR: Could not get stacktrace]";
}

void Exception::rethrowToRuntime() const {
    auto isolate = EngineScope::currentRuntimeIsolateChecked();
    isolate->ThrowException(ctx_->exception.Get(isolate));
}

void Exception::extractMessage() const noexcept {
    if (!ctx_->message.empty()) {
        return;
    }
    auto isolate = EngineScope::currentRuntimeIsolateChecked();
    auto vtry    = v8::TryCatch{isolate};

    auto msg = v8::Exception::CreateMessage(isolate, ctx_->exception.Get(isolate));
    if (!msg.IsEmpty()) {
        Local<String> jsStr{msg->Get()};
        ctx_->message = jsStr.toString().getValue();
        return;
    }
    ctx_->message = "[ERROR: Could not get exception message]";
}

void Exception::makeException() const {
    auto isolate = EngineScope::currentRuntimeIsolateChecked();

    v8::Local<v8::Value> exception;
    {
        switch (ctx_->type) {
        case Type::Unknown:
        case Type::Error:
            exception =
                v8::Exception::Error(v8::String::NewFromUtf8(isolate, ctx_->message.c_str()).ToLocalChecked());
            break;
        case Type::RangeError:
            exception = v8::Exception::RangeError(
                v8::String::NewFromUtf8(isolate, ctx_->message.c_str()).ToLocalChecked()
            );
            break;
        case Type::ReferenceError:
            exception = v8::Exception::ReferenceError(
                v8::String::NewFromUtf8(isolate, ctx_->message.c_str()).ToLocalChecked()
            );
            break;
        case Type::SyntaxError:
            exception = v8::Exception::SyntaxError(
                v8::String::NewFromUtf8(isolate, ctx_->message.c_str()).ToLocalChecked()
            );
            break;
        case Type::TypeError:
            exception = v8::Exception::TypeError(
                v8::String::NewFromUtf8(isolate, ctx_->message.c_str()).ToLocalChecked()
            );
            break;
        }
    }
    ctx_->exception = v8::Global<v8::Value>(isolate, exception);
}

void Exception::rethrow(v8::TryCatch const& tryCatch) {
    if (tryCatch.HasCaught()) {
        throw Exception(tryCatch);
    }
}


} // namespace v8kit

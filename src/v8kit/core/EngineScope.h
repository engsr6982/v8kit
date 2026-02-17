#pragma once
#include "v8kit/Macro.h"

V8KIT_WARNING_GUARD_BEGIN
#include <v8-context.h>
#include <v8-isolate.h>
#include <v8-locker.h>
#include <v8.h>
V8KIT_WARNING_GUARD_END


namespace v8kit {

class Engine;

class EngineScope final {
public:
    explicit EngineScope(Engine& runtime);
    explicit EngineScope(Engine* runtime);
    ~EngineScope();

    V8KIT_DISABLE_COPY_MOVE(EngineScope);
    V8KIT_DISABLE_NEW();

    static Engine* currentRuntime();

    static Engine& currentRuntimeChecked();

    static std::tuple<v8::Isolate*, v8::Local<v8::Context>> currentIsolateAndContextChecked();

    static v8::Isolate* currentRuntimeIsolateChecked();

    static v8::Local<v8::Context> currentRuntimeContextChecked();

private:
    // 作用域链
    Engine const* engine_{nullptr};
    EngineScope*  prev_{nullptr};

    // v8作用域
    v8::Locker         locker_;
    v8::Isolate::Scope isolateScope_;
    v8::HandleScope    handleScope_;
    v8::Context::Scope contextScope_;

    static thread_local EngineScope* gCurrentScope_;
};

class ExitEngineScope final {
    v8::Unlocker unlocker_;

public:
    explicit ExitEngineScope();
    ~ExitEngineScope() = default;

    V8KIT_DISABLE_COPY_MOVE(ExitEngineScope);
    V8KIT_DISABLE_NEW();
};


namespace internal {

class V8EscapeScope final {
    v8::EscapableHandleScope handleScope_;

public:
    explicit V8EscapeScope();
    explicit V8EscapeScope(v8::Isolate* isolate);
    ~V8EscapeScope() = default;

    template <typename T>
    v8::Local<T> escape(v8::Local<T> value) {
        return handleScope_.Escape(value);
    }
};

} // namespace internal

} // namespace v8kit
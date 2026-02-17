#include "EngineScope.h"
#include "Engine.h"

#include <stdexcept>


namespace v8kit {

thread_local EngineScope* EngineScope::gCurrentScope_ = nullptr;

EngineScope::EngineScope(Engine& runtime) : EngineScope(&runtime) {}
EngineScope::EngineScope(Engine* runtime)
: engine_(runtime),
  prev_(gCurrentScope_),
  locker_(runtime->isolate_),
  isolateScope_(runtime->isolate_),
  handleScope_(runtime->isolate_),
  contextScope_(runtime->context_.Get(runtime->isolate_)) {
    gCurrentScope_ = this;
}

EngineScope::~EngineScope() { gCurrentScope_ = prev_; }

Engine* EngineScope::currentRuntime() {
    if (gCurrentScope_) {
        return const_cast<Engine*>(gCurrentScope_->engine_);
    }
    return nullptr;
}

Engine& EngineScope::currentRuntimeChecked() {
    auto current = currentRuntime();
    if (current == nullptr) {
        throw std::logic_error("No EngineScope active");
    }
    return *current;
}

std::tuple<v8::Isolate*, v8::Local<v8::Context>> EngineScope::currentIsolateAndContextChecked() {
    auto& current = currentRuntimeChecked();
    return std::make_tuple(current.isolate_, current.context_.Get(current.isolate_));
}

v8::Isolate*           EngineScope::currentRuntimeIsolateChecked() { return currentRuntimeChecked().isolate_; }
v8::Local<v8::Context> EngineScope::currentRuntimeContextChecked() {
    auto& current = currentRuntimeChecked();
    return current.context_.Get(current.isolate_);
}


ExitEngineScope::ExitEngineScope() : unlocker_(EngineScope::currentRuntimeChecked().isolate_) {}

namespace internal {

V8EscapeScope::V8EscapeScope() : handleScope_(EngineScope::currentRuntimeChecked().isolate_) {}
V8EscapeScope::V8EscapeScope(v8::Isolate* isolate) : handleScope_(isolate) {}

} // namespace internal


} // namespace v8kit
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

Engine* EngineScope::currentEngine() {
    if (gCurrentScope_) {
        return const_cast<Engine*>(gCurrentScope_->engine_);
    }
    return nullptr;
}

Engine& EngineScope::currentEngineChecked() {
    auto current = currentEngine();
    ensureEngine(current);
    return *current;
}

std::tuple<v8::Isolate*, v8::Local<v8::Context>> EngineScope::currentIsolateAndContextChecked() {
    auto& current = currentEngineChecked();
    return std::make_tuple(current.isolate_, current.context_.Get(current.isolate_));
}

v8::Isolate*           EngineScope::currentEngineIsolateChecked() { return currentEngineChecked().isolate_; }
v8::Local<v8::Context> EngineScope::currentEngineContextChecked() {
    auto& current = currentEngineChecked();
    return current.context_.Get(current.isolate_);
}
void EngineScope::ensureEngine(Engine* engine) {
    if (engine == nullptr) {
        throw std::logic_error("An EngineScope must be created before accessing the engine API");
    }
}


ExitEngineScope::ExitEngineScope() : unlocker_(EngineScope::currentEngineChecked().isolate_) {}

namespace internal {

V8EscapeScope::V8EscapeScope() : handleScope_(EngineScope::currentEngineChecked().isolate_) {}
V8EscapeScope::V8EscapeScope(v8::Isolate* isolate) : handleScope_(isolate) {}

} // namespace internal


} // namespace v8kit
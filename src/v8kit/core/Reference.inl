#pragma once
#include "Engine.h"
#include "EngineScope.h"
#include "Reference.h" // NOLINT
#include "ValueHelper.h"

#include <stdexcept>

namespace v8kit {

template <typename T>
    requires concepts::WrapType<T>
Local<T> Local<Value>::as() const {
    if constexpr (std::is_same_v<T, Value>) {
        return asValue();
    } else if constexpr (std::is_same_v<T, Null>) {
        return asNull();
    } else if constexpr (std::is_same_v<T, Undefined>) {
        return asUndefined();
    } else if constexpr (std::is_same_v<T, Boolean>) {
        return asBoolean();
    } else if constexpr (std::is_same_v<T, Number>) {
        return asNumber();
    } else if constexpr (std::is_same_v<T, BigInt>) {
        return asBigInt();
    } else if constexpr (std::is_same_v<T, String>) {
        return asString();
    } else if constexpr (std::is_same_v<T, Symbol>) {
        return asSymbol();
    } else if constexpr (std::is_same_v<T, Function>) {
        return asFunction();
    } else if constexpr (std::is_same_v<T, Object>) {
        return asObject();
    } else if constexpr (std::is_same_v<T, Array>) {
        return asArray();
    }
    [[unlikely]] throw std::logic_error("Unable to convert Local<Value> to T, forgot to add if branch?");
}


// Global<T>
template <typename T>
Global<T>::Global() noexcept = default;

template <typename T>
Global<T>::Global(Local<T> const& val)
: engine_(EngineScope::currentRuntime()),
  handle_(engine_->isolate_, ValueHelper::unwrap(val)) {}

template <typename T>
Global<T>::Global(Weak<T> const& val) : engine_(EngineScope::currentRuntime()),
                                        handle_(ValueHelper::unwrap(val)) {}

template <typename T>
Global<T>::Global(Global<T>&& other) noexcept {
    engine_       = other.engine_;
    handle_       = std::move(other.handle_);
    other.engine_ = nullptr;
}

template <typename T>
Global<T>& Global<T>::operator=(Global<T>&& other) noexcept {
    if (&other != this) {
        engine_       = other.engine_;
        handle_       = std::move(other.handle_);
        other.engine_ = nullptr;
    }
    return *this;
}

template <typename T>
Global<T>::~Global() {
    reset();
}

template <typename T>
Local<T> Global<T>::get() const {
    return Local<T>{handle_.Get(engine_->isolate_)};
}

template <typename T>
Local<Value> Global<T>::getValue() const {
    return Local<Value>{handle_.Get(engine_->isolate_).template As<v8::Value>()};
}

template <typename T>
bool Global<T>::isEmpty() const {
    return handle_.IsEmpty();
}

template <typename T>
void Global<T>::reset() {
    handle_.Reset();
    engine_ = nullptr;
}


// Weak<T>
template <typename T>
Weak<T>::Weak() noexcept = default;

template <typename T>
Weak<T>::Weak(Local<T> const& val)
: engine_(EngineScope::currentRuntime()),
  handle_(engine_->isolate_, ValueHelper::unwrap(val)) {
    markWeak();
}

template <typename T>
Weak<T>::Weak(Global<T> const& val)
: engine_(EngineScope::currentRuntime()),
  handle_(engine_->isolate_, ValueHelper::unwrap(val)) {
    markWeak();
}

template <typename T>
Weak<T>::Weak(Weak<T>&& other) noexcept {
    engine_ = other.engine_;
    handle_ = std::move(other.handle_);
    markWeak();
    other.engine_ = nullptr;
}

template <typename T>
Weak<T>& Weak<T>::operator=(Weak<T>&& other) noexcept {
    if (&other != this) {
        engine_ = other.engine_;
        handle_ = std::move(other.handle_);
        markWeak();
        other.engine_ = nullptr;
    }
    return *this;
}

template <typename T>
Weak<T>::~Weak() {
    reset();
}

template <typename T>
Local<T> Weak<T>::get() const {
    return Local<T>{handle_.Get(engine_->isolate_)};
}

template <typename T>
Local<Value> Weak<T>::getValue() const {
    return Local<Value>{handle_.Get(engine_->isolate_).template As<v8::Value>()};
}

template <typename T>
bool Weak<T>::isEmpty() const {
    return handle_.IsEmpty();
}

template <typename T>
void Weak<T>::reset() {
    handle_.Reset();
    engine_ = nullptr;
}

template <typename T>
void Weak<T>::markWeak() {
    if (!handle_.IsEmpty()) {
        handle_.SetWeak();
    }
}


} // namespace v8kit
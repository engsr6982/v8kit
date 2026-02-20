#pragma once
#include "v8kit/core/Fwd.h"

#include "ReturnValuePolicy.h"

namespace v8kit::binding {


template <typename... Args>
Local<Value> call(Local<Function> const& func, Local<Value> const& thisArg, Args&&... args);

template <typename... Args>
Local<Value> callAsConstructor(Local<Function> const& func, Args&&... args);

template <concepts::Callable F>
FunctionCallback cpp_func(F&& fn, ReturnValuePolicy policy = ReturnValuePolicy::kAutomatic);

template <concepts::Callable... F>
FunctionCallback overload_func(F&&... args);


} // namespace v8kit::binding

#include "BindingUtils.inl" // NOLINT
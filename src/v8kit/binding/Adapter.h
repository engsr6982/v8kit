#pragma once
#include "ReturnValuePolicy.h"
#include "v8kit/core/Fwd.h"

namespace v8kit::binding {

namespace adapter {

template <typename R, typename... Args>
decltype(auto) wrapScriptCallback(Local<Value> const& value);

template <typename Fn>
FunctionCallback wrapFunction(Fn&& fn, ReturnValuePolicy policy);

template <typename... Overload>
FunctionCallback wrapOverloadFunction(ReturnValuePolicy policy, Overload&&... fn);

template <typename... Overload>
FunctionCallback wrapOverloadFuncAndExtraPolicy(Overload&&... fn);


template <typename Fn>
GetterCallback wrapGetter(Fn&& getter, ReturnValuePolicy policy);

template <typename Fn>
SetterCallback wrapSetter(Fn&& setter);

template <typename Ty, bool forceReadonly = false>
std::pair<GetterCallback, SetterCallback> wrapStaticMember(Ty&& member, ReturnValuePolicy policy);

} // namespace adapter


} // namespace v8kit::binding

#include "Adapter.inl" // NOLINT
#pragma once
#include "ValueHelper.h"

namespace v8kit {

template <typename T>
    requires concepts::WrapType<T>
v8::Local<internal::V8Type_v<T>> ValueHelper::unwrap(Local<T> const& value) {
    return value.val; // friend
}
template <typename T>
Local<T> ValueHelper::wrap(v8::Local<internal::V8Type_v<T>> const& value) {
    return Local<T>{value};
}

} // namespace v8kit
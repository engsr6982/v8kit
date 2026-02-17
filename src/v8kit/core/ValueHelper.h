#pragma once
#include "Concepts.h"
#include "V8TypeAlias.h"


namespace v8kit {


struct ValueHelper {
    ValueHelper() = delete;

    template <typename T>
        requires concepts::WrapType<T>
    [[nodiscard]] inline static v8::Local<internal::V8Type_v<T>> unwrap(Local<T> const& value);

    template <typename T>
    [[nodiscard]] inline static Local<T> wrap(v8::Local<internal::V8Type_v<T>> const& value);
};


} // namespace v8kit

#include "ValueHelper.inl"

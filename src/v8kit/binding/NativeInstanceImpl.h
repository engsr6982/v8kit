#pragma once
#include "traits/TypeTraits.h"
#include "v8kit/core/Exception.h"
#include "v8kit/core/MetaInfo.h"
#include "v8kit/core/NativeInstance.h"

#include <memory>
#include <type_traits>
#include <typeindex>


namespace v8kit::binding {

/**
 * @brief 具体类型的实例持有者
 *
 * @tparam T 实际的 C++ 类型 (e.g., MyClass)
 * @tparam Holder 实际的持有方式 (e.g., T*, std::unique_ptr<T>, std::shared_ptr<T>)
 */
template <typename T, typename Holder>
class NativeInstanceImpl final : public NativeInstance {
public:
    // 从 Holder 推导元素类型 (处理 T* vs shared_ptr<T> 的区别)
    using ElementType = typename std::pointer_traits<
        typename std::conditional<std::is_pointer_v<Holder>, Holder, typename Holder::element_type*>::type>::
        element_type;

    Holder value_;
    bool   takeOwnership_ = false;

    explicit NativeInstanceImpl(ClassMeta const* meta, Holder value)
        requires traits::is_unique_ptr_v<T>
    : NativeInstance(meta),
      value_(std::move(value)),
      takeOwnership_(true) {}

    explicit NativeInstanceImpl(ClassMeta const* meta, Holder value, bool takeOwnership)
    : NativeInstance(meta),
      value_(std::move(value)),
      takeOwnership_(takeOwnership) {}

    std::type_index type_id() const override { return std::type_index(typeid(std::remove_cv_t<ElementType>)); }

    bool is_const() const override { return std::is_const_v<ElementType>; }

    void* cast(std::type_index target_type) const override {
        ElementType* ptr = get_raw_ptr();
        if (!ptr) return nullptr;

        if (target_type == std::type_index(typeid(std::remove_cv_t<ElementType>))) {
            return const_cast<void*>(static_cast<const void*>(ptr));
        }

        // 2. 继承路径：利用 ClassMeta 链进行安全的指针偏移
        // 注意：这里传入的是 ptr (T*)，meta_->castTo 会利用 upcaster 一层层转上去
        // 即使 ptr 是多重继承的中间部分，upcaster 也能把它修成 Base*
        if (meta_) {
            // 必须将 T* 转为 void* 传入，假设 Meta 注册的是 T 的 Meta
            // 这里有个前提：meta_ 必须对应 T。如果 T 是多态获取的 Derived，meta_ 是 DerivedMeta。
            return meta_->castTo(static_cast<void*>(ptr), target_type);
        }
        return nullptr;
    }

    std::shared_ptr<void> get_shared_ptr() const override {
        if constexpr (traits::is_shared_ptr_v<Holder>) {
            return value_;
        }
        return nullptr;
    }

    std::unique_ptr<NativeInstance> clone() const override {
        if constexpr (std::is_copy_constructible_v<ElementType>) {
            auto* raw = get_raw_ptr();
            if (raw) {
                return std::make_unique<NativeInstanceImpl<ElementType, std::unique_ptr<ElementType>>>(
                    meta_,
                    std::make_unique<ElementType>(*raw)
                );
            }
        }
        throw Exception("Object is not copy constructible");
    }

    bool is_owned() const override { return takeOwnership_; }

private:
    ElementType* get_raw_ptr() const {
        if constexpr (std::is_pointer_v<Holder>) {
            return value_;
        } else {
            return value_.get();
        }
    }
};


namespace factory {






}


} // namespace v8kit::binding
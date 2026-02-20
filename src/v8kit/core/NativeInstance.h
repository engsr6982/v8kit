#pragma once
#include <typeindex>

namespace v8kit {

struct ClassMeta;

class NativeInstance {
protected:
    ClassMeta const* meta_;

public:
    explicit NativeInstance(ClassMeta const* meta) : meta_(meta) {}
    virtual ~NativeInstance() = default;

    V8KIT_DISABLE_COPY(NativeInstance);

    [[nodiscard]] inline ClassMeta const* meta() const { return meta_; }

    virtual std::type_index type_id() const = 0;

    virtual bool is_const() const = 0;

    virtual void* cast(std::type_index target_type) const = 0;

    virtual std::shared_ptr<void> get_shared_ptr() const { return nullptr; }

    virtual std::unique_ptr<NativeInstance> clone() const = 0;

    virtual bool is_owned() const = 0;

    template <typename T>
    T* unwrap() const {
        if (is_const() && !std::is_const_v<T>) {
            throw Exception("Cannot unwrap const instance to mutable pointer");
        }
        void* raw = cast(std::type_index(typeid(std::remove_cv_t<T>)));
        return static_cast<T*>(raw);
    }
};


} // namespace v8kit
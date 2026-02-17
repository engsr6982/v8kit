#pragma once

namespace v8kit {

class NativeInstance {
protected:
    ClassMeta const* meta_;

public:
    explicit NativeInstance(ClassMeta const* meta) : meta_(meta) {}
    virtual ~NativeInstance() = default;

    V8KIT_DISABLE_COPY(NativeInstance);
};


} // namespace v8kit
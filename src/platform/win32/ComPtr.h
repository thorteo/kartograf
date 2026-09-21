#pragma once

namespace tarkov::win32 {

/// A minimal intrusive COM pointer.
///
/// Hand-rolled rather than pulled from WRL or ATL so the Windows layer depends
/// on nothing beyond the plain SDK headers, which keeps the clang-cl cross
/// build simple.
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(const ComPtr& other) : ptr_(other.ptr_) {
        if (ptr_) ptr_->AddRef();
    }
    ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
    ~ComPtr() { reset(); }

    ComPtr& operator=(const ComPtr& other) {
        if (this != &other) {
            reset();
            ptr_ = other.ptr_;
            if (ptr_) ptr_->AddRef();
        }
        return *this;
    }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    void reset() {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

    T* get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

    /// For the out-parameter of a Create* call. Releases anything held first.
    T** put() {
        reset();
        return &ptr_;
    }
    void** putVoid() { return reinterpret_cast<void**>(put()); }

private:
    T* ptr_ = nullptr;
};

}  // namespace tarkov::win32

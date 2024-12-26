//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <containers/allocators/arena_allocator.h>

#include <cassert>
#include <cstdlib>
#include <limits>
#include <memory>

#include <sys/mman.h>

namespace containers {

class small_ptr_mmap_arena {
    void* buffer_ = nullptr;
    intptr_t buffer_size_ = 0;

    struct state {
        intptr_t ptr_ = 0;
        intptr_t end_ = 0;
        intptr_t allocated_ = 0;
    };

    state state_;

public:
    using resource_mark_type = resource_mark< small_ptr_mmap_arena, state >;

    small_ptr_mmap_arena(std::size_t size)
        : buffer_size_(size)
    {}

    ~small_ptr_mmap_arena() {
        if (buffer_)
            munmap(buffer_, buffer_size_);
    }

    void* allocate(intptr_t size, intptr_t alignment) {
        assert((alignment & (alignment - 1)) == 0);
        intptr_t padding = -(uintptr_t)state_.ptr_ & (alignment - 1);
        intptr_t capacity = state_.end_ - state_.ptr_ - padding;
        if (capacity < size) {
            if (std::numeric_limits<intptr_t>::max() - (alignment - 1) < size)
                return nullptr;

            if (!buffer_) {
                if (buffer_size_ - padding < size)
                    return nullptr;
                auto buffer = mmap(0, buffer_size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if (buffer == MAP_FAILED)
                    return nullptr;
                buffer_ = buffer;
            }

            state_.ptr_ = (intptr_t)buffer_;
            state_.end_ = state_.ptr_ + buffer_size_;
            padding = -(uintptr_t)state_.ptr_ & (alignment - 1);
            capacity = state_.end_ - state_.ptr_ - padding;
            if (capacity < size)
                return nullptr;

            assert(size <= state_.end_ - state_.ptr_ - padding);
        }

        intptr_t ptr = state_.ptr_ + padding;
        assert((ptr & (alignment - 1)) == 0);
        state_.ptr_ = ptr + size;
        state_.allocated_ += size;
        return reinterpret_cast<void*>(ptr);
    }

    void deallocate(void*, std::size_t size) {
        state_.allocated_ -= size;
        assert(state_.allocated_ >= 0);
        if (state_.allocated_ == 0) {
            state_.ptr_ = (intptr_t)buffer_;
            state_.end_ = state_.ptr_ + buffer_size_;
        }
    }

    state get_state() const { return state_; }
    void set_state(const state& s) { state_ = s; }
};

template< typename T > class small_ptr {
    T* ptr_ = nullptr;
    small_ptr(T *ptr, bool) : ptr_(ptr) {}

public:
    using element_type = T;
    using difference_type = std::ptrdiff_t;
    using value_type = element_type;
    using pointer = element_type*;
    using reference = element_type&;
    using iterator_category = std::random_access_iterator_tag;

    small_ptr() = default;
    small_ptr(const small_ptr<T>&) = default;

    small_ptr<T>& operator=(const small_ptr<T>&) = default;

    static small_ptr<T> pointer_to(element_type& r) noexcept {
        return small_ptr<T>(std::addressof(r), true);
    }

    // allocator::pointer is convertible to allocator::const_pointer
    template<typename U = T, typename std::enable_if_t<std::is_const_v<U>, int> = 0>
    small_ptr(const small_ptr<typename std::remove_const_t<T>>& p) : ptr_(p.operator->()  /* std::to_address(p) in C++20 */) {}

    // NullablePointer
    small_ptr(std::nullptr_t) : small_ptr() {}
    small_ptr& operator=(std::nullptr_t) {
        ptr_ = nullptr;
        return *this;
    }
    explicit operator bool() const { return *this != nullptr; }

    pointer operator->() const { return ptr_; }

    element_type& operator*() const { return *ptr_; }

    small_ptr<T>& operator++() {
        ++ptr_;
        return *this;
    }

    friend bool operator==(small_ptr<T> l, small_ptr<T> r) { return l.ptr_ == r.ptr_; }
    friend bool operator!=(small_ptr<T> l, small_ptr<T> r) { return !(l == r); }

    small_ptr<T> operator++(int) { return small_ptr<T>(ptr_++, true); }

    small_ptr<T>& operator--() {
        --ptr_;
        return *this;
    }

    small_ptr<T> operator--(int) { return FancyPtr(ptr_--, true); }

    small_ptr<T>& operator+=(difference_type n) {
        ptr_ += n;
        return *this;
    }

    friend small_ptr<T> operator+(small_ptr<T> p, difference_type n) { return p += n; }

    friend small_ptr<T> operator+(difference_type n, small_ptr<T> p) { return p += n; }

    small_ptr<T>& operator-=(difference_type n) {
        ptr_ -= n;
        return *this;
    }

    friend small_ptr<T> operator-(small_ptr<T> p, difference_type n) { return p -= n; }

    friend difference_type operator-(small_ptr<T> a, small_ptr<T> b) { return a.ptr_ - b.ptr_; }

    reference operator[](difference_type n) const { return ptr_[n]; }

    friend bool operator<(small_ptr<T> a, small_ptr<T> b) { return std::less<pointer>(a.ptr_, b.ptr_); }

    friend bool operator> (small_ptr<T> a, small_ptr<T> b) { return b < a; }
    friend bool operator>=(small_ptr<T> a, small_ptr<T> b) { return !(a < b); }
    friend bool operator<=(small_ptr<T> a, small_ptr<T> b) { return !(b < a); }

#if defined(_LIBCPP_MEMORY)
    // Extra libc++ requirement (Since libc++ does `static_cast<FancyPtr<U>>(FancyPtr<T>())` sometimes)
    template<typename U> small_ptr(small_ptr<U> p) : ptr_(static_cast<T*>(p.operator->())) {}
#elif defined(_GLIBCXX_MEMORY)
    // Extra libstdc++ requirement (Since libstdc++ uses raw pointers internally and tries to implicitly cast back
    // and also casts from pointers to different types)
    template<typename U> small_ptr(small_ptr<U> p) : ptr_(static_cast<T*>(p.operator->())) {}
    small_ptr(T *ptr) : small_ptr(ptr, true) {}
    operator T*() { return ptr_; }
#endif
};

template< typename T > bool operator==(small_ptr<T> p, std::nullptr_t) { return p == small_ptr<T>(); }
template< typename T > bool operator==(std::nullptr_t, small_ptr<T> p) { return small_ptr<T>() == p; }
template< typename T > bool operator!=(small_ptr<T> p, std::nullptr_t) { return p != small_ptr<T>(); }
template< typename T > bool operator!=(std::nullptr_t, small_ptr<T> p) { return small_ptr<T>() != p; }

template<> class small_ptr<void> {
    void* ptr_ = nullptr;
    small_ptr(void *ptr, bool) : ptr_(ptr) {}
public:
    using element_type = void;
    using pointer = void*;

    small_ptr() = default;
    small_ptr(const small_ptr&) = default;

    template<typename T, typename std::enable_if_t<!std::is_const_v<T>, int> = 0>
    small_ptr(small_ptr<T> p) : ptr_(static_cast<void*>(p.operator->())) {}

    small_ptr& operator=(const small_ptr<void>&) = default;

    small_ptr& operator=(std::nullptr_t) {
        ptr_ = nullptr;
        return *this;
    }

    pointer operator->() const { return ptr_; }

    template<typename T>
    explicit operator small_ptr<T>() {
        if (ptr_ == nullptr) return nullptr;
        return std::pointer_traits<small_ptr<T>>::pointer_to(*static_cast<T*>(ptr_));
    }
};

template<> class small_ptr<const void> {
    const void* ptr_ = nullptr;
    small_ptr(const void *ptr, bool) : ptr_(ptr) {}
public:
    using element_type = const void;
    using pointer = const void*;

    small_ptr() = default;
    small_ptr(const small_ptr&) = default;

    template<typename T, typename std::enable_if_t<!std::is_const_v<T>, int> = 0>
    small_ptr(small_ptr<T> p) : ptr_(static_cast<void*>(p.operator->())) {}

    small_ptr& operator=(const small_ptr&) = default;

    small_ptr& operator=(std::nullptr_t) {
        ptr_ = nullptr;
        return *this;
    }

    pointer operator->() const { return ptr_; }

    template<typename T>
    explicit operator small_ptr<T>() {
        if (ptr_ == nullptr) return nullptr;
        return std::pointer_traits<small_ptr<T>>::pointer_to(*static_cast<T*>(ptr_));
    }
};

template <typename T, typename Arena = small_ptr_mmap_arena > class small_ptr_arena_allocator {
    template <typename U, typename ArenaU> friend class small_ptr_arena_allocator;
    Arena* arena_ = nullptr;

public:
    using pointer = small_ptr<T>;
    using value_type    = T;
    using resource_mark_type = typename Arena::resource_mark_type;

    small_ptr_arena_allocator(Arena& arena) noexcept
        : arena_(&arena) {}

    template <typename U> small_ptr_arena_allocator(const small_ptr_arena_allocator<U, Arena>& other) noexcept
        : arena_(other.arena_) {}

    pointer allocate(std::size_t n) {
        if (std::numeric_limits<intptr_t>::max() / sizeof(T) < n)
            return nullptr;
        return reinterpret_cast<value_type*>(arena_->allocate(sizeof(T) * n, alignof(T)));
    }

    void deallocate(pointer ptr, std::size_t n) noexcept {
        arena_->deallocate(ptr.operator->(), sizeof(T) * n);
    }

    resource_mark_type resource_mark() { return arena_; }
};

template <typename T, typename U, typename Arena>
bool operator == (const small_ptr_arena_allocator<T, Arena>& lhs, const small_ptr_arena_allocator<U, Arena>& rhs) noexcept {
    return lhs.arena_ == rhs.arena_;
}

template <typename T, typename U, typename Arena>
bool operator != (const small_ptr_arena_allocator<T, Arena>& x, const small_ptr_arena_allocator<U, Arena>& y) noexcept {
    return !(x == y);
}

}

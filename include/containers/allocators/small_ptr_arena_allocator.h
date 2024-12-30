//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <containers/allocators/arena_allocator.h>

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory>

#include <sys/mman.h>

namespace containers {

template< std::size_t MinSize = 1, std::size_t MinAlignment = alignof(std::max_align_t) > class small_ptr_mmap_arena {
    void* buffer_ = nullptr;
    intptr_t buffer_size_ = 0;

    struct state {
        intptr_t ptr_ = 0;
        intptr_t end_ = 0;
        intptr_t allocated_ = 0;
    };

    state state_;

public:
    static constexpr std::size_t MinAllocationSize = MinSize;

    using resource_mark_type = resource_mark< small_ptr_mmap_arena, state >;

    small_ptr_mmap_arena(std::size_t size)
        : buffer_size_(size)
    {}

    ~small_ptr_mmap_arena() {
        if (buffer_)
            munmap(buffer_, buffer_size_);
    }

    uint32_t allocate(intptr_t size, intptr_t align) {
        intptr_t alignment = std::max<intptr_t>(align, MinAlignment);
        assert((alignment & (alignment - 1)) == 0);
        intptr_t padding = -(uintptr_t)state_.ptr_ & (alignment - 1);
        intptr_t capacity = state_.end_ - state_.ptr_ - padding;
        if (capacity < size) {
            if (std::numeric_limits<intptr_t>::max() - (alignment - 1) < size)
                return 0;

            if (!buffer_) {
                if (buffer_size_ - intptr_t(MinAlignment - 1) < size)
                    return { nullptr_index() };
                auto buffer = mmap(0, buffer_size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if (buffer == MAP_FAILED)
                    return 0;
                buffer_ = buffer;
            }

            state_.ptr_ = (intptr_t)buffer_ + 1;
            state_.end_ = state_.ptr_ + buffer_size_ - 1;
            padding = -(uintptr_t)state_.ptr_ & (alignment - 1);
            capacity = state_.end_ - state_.ptr_ - padding;
            if (capacity < size)
                return 0;

            assert(size <= state_.end_ - state_.ptr_ - padding);
        }

        intptr_t ptr = state_.ptr_ + padding;
        assert((ptr & (alignment - 1)) == 0);
        state_.ptr_ = ptr + size;
        state_.allocated_ += size;
        return static_cast<uint32_t>(ptr - (intptr_t)buffer_);
    }

    void deallocate(uint32_t, std::size_t size) {
        state_.allocated_ -= size;
        assert(state_.allocated_ >= 0);
        if (state_.allocated_ == 0) {
            state_.ptr_ = (intptr_t)buffer_ + 1;
            state_.end_ = state_.ptr_ + buffer_size_ - 1;
        }
    }

    void* address(uint32_t index) {
        assert(index < buffer_size_);
        if (index == 0)
            return nullptr;

        // TODO: alignment
        return reinterpret_cast<void*>((intptr_t)buffer_ + index);
    }

    uint32_t index(uint32_t size, uint32_t n) {
        return size * n;
    }

    uint32_t index(void* ptr) {
        assert(ptr);
        assert((intptr_t)ptr - (intptr_t)buffer_ > 0);
        assert((intptr_t)ptr - (intptr_t)buffer_ < buffer_size_);

        return (intptr_t)ptr - (intptr_t)buffer_;
    }

    ptrdiff_t difference(std::size_t size, uint32_t a, uint32_t b) {
        return ((ptrdiff_t)a - (ptrdiff_t)b)/size;
    }

    uint32_t nullptr_index() { return 0; }

    state get_state() const { return state_; }
    void set_state(const state& s) { state_ = s; }
};

#if 1
template< typename T, typename Factory > class small_ptr {
    static_assert(sizeof(T) >= Factory::arena_type::MinAllocationSize);

    template <typename U, typename FactoryU> friend class small_ptr_arena_allocator;
    uint32_t index_ = 0;

    small_ptr(uint32_t index) : index_(index) {}
public:
    using element_type = T;
    using difference_type = std::ptrdiff_t;
    using value_type = element_type;
    using pointer = element_type*;
    using reference = element_type&;
    using iterator_category = std::random_access_iterator_tag;

    small_ptr() = default;
    small_ptr(const small_ptr&) = default;
    small_ptr& operator = (const small_ptr&) = default;

    small_ptr(T *ptr): index_(Factory::get()->index(ptr)) {}

    // TODO: need to check that no inheritabce displacement is present
    //template<typename U> small_ptr(small_ptr<U> p) : index_(p.index_) {}

    template<typename U = T, typename std::enable_if_t<std::is_const_v<U>, int> = 0>
    small_ptr(const small_ptr<typename std::remove_const_t<T>, Factory>& p) : index_(p.index_) {}

    small_ptr(std::nullptr_t): small_ptr() {}

    small_ptr& operator = (std::nullptr_t) {
        index_ = 0;
        return *this;
    }

    explicit operator bool() const { return index_ != 0; }

    operator T*() { return operator ->(); }
    operator const T*() const { return operator ->(); }

    pointer operator->() const { return static_cast<T*>(Factory::get()->address(index_)); }

    element_type& operator*() const { return *operator->(); }

    small_ptr& operator++() {
        index_ += Factory::get()->index(sizeof(T), 1);
        return *this;
    }

    small_ptr operator++ (int) {
        small_ptr p(index_);
        index_ += Factory::get()->index(sizeof(T), 1);
        return p;
    }

    small_ptr& operator--() {
        index_ -= Factory::get()->index(sizeof(T), 1);
        return *this;
    }

    small_ptr operator--(int) {
        small_ptr p(index_);
        index_ -= Factory::get()->index(sizeof(T), 1);
        return p;
    }

    friend bool operator == (small_ptr l, small_ptr r) {
        return l.index_ == r.index_;
    }

    friend bool operator != (small_ptr l, small_ptr r) {
        return !(l == r);
    }

    small_ptr& operator += (difference_type n) {
        index_ += Factory::get()->index(sizeof(T), n);
        return *this;
    }

    small_ptr& operator -= (difference_type n) {
        index_ -= Factory::get()->index(sizeof(T), n);
        return *this;
    }

    friend small_ptr operator + (small_ptr p, difference_type n) {
        return p.index_ + Factory::get()->index(sizeof(T), n);
    }

    friend small_ptr operator + (difference_type n, small_ptr p) {
        return p.index_ + Factory::get()->index(sizeof(T), n);
    }

    friend small_ptr operator - (small_ptr p, difference_type n) {
        return p.index_ - Factory::get()->index(sizeof(T), n);
    }

    friend difference_type operator - (small_ptr a, small_ptr b) {
        return Factory::get()->difference(sizeof(T), a.index_, b.index_);
    }

    reference operator[](difference_type n) const { return operator->()[n]; }

    friend bool operator < (small_ptr a, small_ptr b) {
        return a.index_ < b.index_;
    }

    friend bool operator > (small_ptr a, small_ptr b) {
        return b.index_ < a.index_;
    }

    friend bool operator >= (small_ptr a, small_ptr b) {
        return !(a.index_ < b.index_);
    }

    friend bool operator <= (small_ptr a, small_ptr b) {
        return !(b.index_ < a.index_);
    }

    friend bool operator == (small_ptr p, std::nullptr_t) {
        return p.index_ == 0;
    }

    friend bool operator == (std::nullptr_t, small_ptr p) {
        return p.index_ == 0;
    }

    friend bool operator != (small_ptr p, std::nullptr_t) {
        return p.index_ != 0;
    }

    friend bool operator != (std::nullptr_t, small_ptr p) {
        return p.index_ != 0;
    }
};

template< typename Factory > class small_ptr<void, Factory> {
    uint32_t index_ = 0;
    small_ptr(uint32_t index) : index_(index) {}
public:
    using element_type = void;
    using pointer = void*;

    small_ptr() = default;
    small_ptr(const small_ptr&) = default;

    template<typename T, typename std::enable_if_t<!std::is_const_v<T>, int> = 0>
    small_ptr(const small_ptr<T, Factory>& p) : index_(p.index_) {}

    small_ptr& operator=(const small_ptr<void, Factory>&) = default;

    small_ptr& operator=(std::nullptr_t) {
        index_ = 0;
        return *this;
    }

    pointer operator->() const { return Factory::get()->address(index_); }

    template<typename T> explicit operator small_ptr<T, Factory>() { return index_; }
};

template< typename Factory > class small_ptr<const void, Factory> {
    uint32_t index_ = 0;
    small_ptr(uint32_t index) : index_(index) {}

public:
    using element_type = const void;
    using pointer = const void*;

    small_ptr() = default;
    small_ptr(const small_ptr&) = default;

    template<typename T, typename std::enable_if_t<!std::is_const_v<T>, int> = 0>
    small_ptr(const small_ptr<T, Factory>& p) : index_(p.index_) {}

    small_ptr& operator=(const small_ptr&) = default;

    small_ptr& operator=(std::nullptr_t) {
        index_ = 0;
        return *this;
    }

    pointer operator->() const { return Factory::get()->address(index_); }

    template<typename T> explicit operator small_ptr<T, Factory>() { return index_; }
};

#else
template< typename T, typename ArenaFactory > struct small_ptr {
    uint32_t index_;

    T* operator -> () {
        return static_cast<T*>(ArenaFactory::get()->address(index_));
    }

    T& operator* () {
        return *operator ->();
    }

    small_ptr& operator -= (ptrdiff_t n) {
        index_ -= ArenaFactory::get()->index(sizeof(T), n);
        return *this;
    }
};
#endif

struct small_ptr_mmap_arena_factory {
    using arena_type = small_ptr_mmap_arena<>;
    using resource_mark_type = arena_type::resource_mark_type;

    static arena_type* get() {
        static thread_local arena_type arena(1<<30);
        return &arena;
    }
};

template <typename T, typename Factory = small_ptr_mmap_arena_factory>
class small_ptr_arena_allocator {
    template <typename U, typename FactoryU> friend class small_ptr_arena_allocator;


public:
    using resource_mark_type = typename Factory::resource_mark_type;

    using pointer = small_ptr<T, Factory>;
    using value_type = T;

    small_ptr_arena_allocator() = default;
    template <typename U> small_ptr_arena_allocator(const small_ptr_arena_allocator<U, Factory>&) noexcept
    {}

    pointer allocate(std::size_t n) {
        if (std::numeric_limits<intptr_t>::max() / sizeof(T) < n)
            return 0u;
        return Factory::get()->allocate(sizeof(T) * n, alignof(T));
    }

    void deallocate(pointer ptr, std::size_t n) noexcept {
        Factory::get()->deallocate(ptr.index_, sizeof(T) * n);
    }

    resource_mark_type resource_mark() { return Factory::get(); }
};

template <typename T, typename U, typename Factory>
bool operator == (const small_ptr_arena_allocator<T, Factory>&, const small_ptr_arena_allocator<U, Factory>&) noexcept {
    return true; // TODO
}

template <typename T, typename U, typename Factory>
bool operator != (const small_ptr_arena_allocator<T, Factory>& x, const small_ptr_arena_allocator<U, Factory>& y) noexcept {
    return !(x == y);
}

}

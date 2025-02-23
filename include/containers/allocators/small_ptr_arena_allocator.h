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
#include <vector>

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
    static_assert(((MinAllocationSize) & (MinAllocationSize - 1)) == 0);

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
                    return 0;
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
        return static_cast<uint32_t>(ptr - (intptr_t)buffer_) / MinAllocationSize;
    }

    void deallocate(uint32_t, std::size_t size) {
        state_.allocated_ -= size;
        assert(state_.allocated_ >= 0);
        if (state_.allocated_ == 0) {
            state_.ptr_ = (intptr_t)buffer_ + 1;
            state_.end_ = state_.ptr_ + buffer_size_ - 1;
        }
    }

    void* address(intptr_t index) {
        index *= MinAllocationSize;
        assert(index < buffer_size_);
        if (index == 0)
            return nullptr;
        return reinterpret_cast<void*>((intptr_t)buffer_ + index);
    }

    uint32_t index(void* ptr) {
        assert(ptr);
        assert((intptr_t)ptr - (intptr_t)buffer_ > 0);
        assert((intptr_t)ptr - (intptr_t)buffer_ < buffer_size_);

        return ((intptr_t)ptr - (intptr_t)buffer_)/MinAllocationSize;
    }

    state get_state() const { return state_; }
    void set_state(const state& s) { state_ = s; }
};

template< std::size_t MinSize, std::size_t BlockSize, typename Allocator = std::allocator<uint8_t> > class small_ptr_arena
    : std::allocator_traits< Allocator >::template rebind_alloc<uint8_t>
{
    using allocator_type = typename allocator_traits< Allocator >::template rebind_alloc<uint8_t>;
    using allocator_traits_type = allocator_traits< allocator_type >;

    struct block {
        uintptr_t size:63;
        uintptr_t owned:1;
    };

    block* block_initial_ = nullptr;

    struct state {
        intptr_t index_size_ = 0;
        intptr_t block_ptr_ = 0;
        intptr_t block_end_ = 0;
        intptr_t allocated_ = 0;
    };

    state state_;

    std::pair<uint32_t, void*> cache_ = {};

    std::vector< std::pair< block*, intptr_t > > index_;

    std::size_t request_block(intptr_t bytes) {
        // For large blocks, glibc's malloc is aligning large allocations
        // to the multiples of page size, also keeping space for chunk size.
        auto header_size = allocator_traits_type::header_size();
        intptr_t size = ((
            header_size +
            std::max<intptr_t>(BlockSize, sizeof(block) + bytes) +
            BlockSize - 1
        ) & ~(BlockSize - 1)) - header_size;
        // intptr_t size = (sizeof(block) + bytes + BlockSize - 1) & ~(BlockSize - 1);
        if (size < 0)
            return -1;
        assert(size > 0);
        assert(size - (intptr_t)sizeof(block) >= bytes);
        auto head = allocate_block(size);
        head->size = size;
        head->owned = true;
        return push_block(head);
    }

    block* allocate_block(intptr_t size) {
        block *ptr = reinterpret_cast<block*>(allocator_traits_type::allocate(*this, size));
        assert((reinterpret_cast<intptr_t>(ptr) & (alignof(block) - 1)) == 0);
        return ptr;
    }

    std::size_t push_block(block* head) {
        state_.block_ptr_ = reinterpret_cast<intptr_t>(head) + sizeof(block);
        state_.block_end_ = reinterpret_cast<intptr_t>(head) + head->size;

        std::size_t index = index_.size();
        for(intptr_t i = 0; i < (intptr_t)head->size; i += BlockSize) {
            index_.emplace_back(std::make_pair<block*, intptr_t>(
                i == 0 ? head : nullptr, state_.block_ptr_ + i * (intptr_t)BlockSize));
        }
        state_.index_size_ = index_.size();
        return index;
    }

    void deallocate_block(block* ptr) {
        assert(ptr->owned);
        allocator_traits_type::deallocate(*this, reinterpret_cast<uint8_t*>(ptr), ptr->size);
    }

    void deallocate_blocks(intptr_t end) {
        for(std::size_t i = end; i < index_.size(); ++i) {
            if (index_[i].first && index_[i].first->owned) {
                deallocate_block(index_[i].first);
            }
        }

        index_.resize(end);
    }

    constexpr std::size_t log2(std::size_t n) { return ((n<2) ? 1 : 1 + log2(n/2)); }

public:
    using resource_mark_type = resource_mark< small_ptr_arena< MinSize, BlockSize, Allocator >, state >;

    static constexpr std::size_t MinAllocationSize = MinSize;
    static_assert(((MinAllocationSize) & (MinAllocationSize - 1)) == 0);

    small_ptr_arena() = default;

    template< typename T, std::size_t N > small_ptr_arena(T(&buffer)[N])
        : small_ptr_arena(reinterpret_cast<uint8_t*>(buffer), N * sizeof(T)) {
        static_assert(std::is_trivial_v<T>);
        static_assert(N * sizeof(T) > sizeof(block));
        assert(N == BlockSize);
    }

    small_ptr_arena(uint8_t* buffer, std::size_t size) {
        assert(size > sizeof(block));
        assert(size == BlockSize);
        auto head = reinterpret_cast<block*>(buffer);
        block_initial_ = head;
        head->owned = false;
        head->size = size;
        push_block(head);
    }

    ~small_ptr_arena() {
        deallocate_blocks(0);
    }

    uint32_t allocate(intptr_t size, intptr_t alignment) {
        assert((alignment & (alignment - 1)) == 0);
        intptr_t padding = -(uintptr_t)state_.block_ptr_ & (alignment - 1);
        intptr_t capacity = state_.block_end_ - state_.block_ptr_ - padding;
        uint32_t hi = 0;
        if (capacity < size) {
            if (std::numeric_limits<intptr_t>::max() - (alignment - 1) < size)
                return 0;
            hi = request_block(size + (alignment - 1));
            if (hi == (uint32_t)-1)
                return 0;
            padding = -(uintptr_t)state_.block_ptr_ & (alignment - 1);
            assert(size <= state_.block_end_ - state_.block_ptr_ - padding);
        } else {
            hi = index_.size() - 1;
        }
        intptr_t ptr = state_.block_ptr_ + padding;
        assert((ptr & (alignment - 1)) == 0);
        state_.block_ptr_ = ptr + size;
        state_.allocated_ += size;
        assert(!index_.empty());

        uint32_t lo = (ptr - index_.back().second);
        cache_.first = (hi << (log2(BlockSize) - 1)) | lo;
        cache_.second = (void*)ptr;

        return (hi << (log2(BlockSize) - 1)) | lo;
    }

    void deallocate(uint32_t, std::size_t size) {
        state_.allocated_ -= size;
        assert(state_.allocated_ >= 0);
        if (state_.allocated_ == 0) {
            deallocate_blocks(0);
            state_ = state();
            if (block_initial_)
                push_block(block_initial_);
        }
    }

    void* address(intptr_t index) {
        if (cache_.first == index)
            return cache_.second;

        index *= MinAllocationSize;
        auto hi = index >> (log2(BlockSize) - 1);
        auto lo = index & (BlockSize - 1);
        return reinterpret_cast<void*>(index_[hi].second + lo);
    }

    uint32_t index(void* ptr) {
        for(std::size_t i = 0; i < index_.size(); ++i) {
            uintptr_t lo = (uintptr_t)ptr - (uintptr_t)index_[i].second;
            if (lo < BlockSize) {
                return (i << (log2(BlockSize) - 1)) | lo;
            }
        }

        std::abort();
    }

    state get_state() const { return state_; }

    void set_state(const state& s) {
        deallocate_blocks(s.index_size_ + 1);
        state_ = s;
    }
};

#if 1
template< typename T, typename Factory > class small_ptr {
    using arena_type = typename Factory::arena_type;
    static_assert(sizeof(T) >= arena_type::MinAllocationSize);

    template <typename U, typename FactoryU, std::size_t AlignmentU> friend class small_ptr_arena_allocator;

    uint32_t index_ = 0;

    small_ptr(uint32_t index) : index_(index) {}

    static uint32_t index(uint32_t size, uint32_t n) {
        return (size * n) / arena_type::MinAllocationSize;
    }

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
        index_ += index(sizeof(T), 1);
        return *this;
    }

    small_ptr operator++ (int) {
        small_ptr p(index_);
        index_ += index(sizeof(T), 1);
        return p;
    }

    small_ptr& operator--() {
        index_ -= index(sizeof(T), 1);
        return *this;
    }

    small_ptr operator--(int) {
        small_ptr p(index_);
        index_ -= index(sizeof(T), 1);
        return p;
    }

    friend bool operator == (small_ptr l, small_ptr r) {
        return l.index_ == r.index_;
    }

    friend bool operator != (small_ptr l, small_ptr r) {
        return !(l == r);
    }

    small_ptr& operator += (difference_type n) {
        index_ += index(sizeof(T), n);
        return *this;
    }

    small_ptr& operator -= (difference_type n) {
        index_ -= index(sizeof(T), n);
        return *this;
    }

    friend small_ptr operator + (small_ptr p, difference_type n) {
        return p.index_ + index(sizeof(T), n);
    }

    friend small_ptr operator + (difference_type n, small_ptr p) {
        return p.index_ + index(sizeof(T), n);
    }

    friend small_ptr operator - (small_ptr p, difference_type n) {
        return p.index_ - index(sizeof(T), n);
    }

    friend difference_type operator - (small_ptr a, small_ptr b) {
        return ((intptr_t)a.index_ - (intptr_t)b.index_) / sizeof(T);
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
        static arena_type arena(1<<30);
        return &arena;
    }
};

template< std::size_t BlockSize = 1 << 16 > struct small_ptr_arena_factory {
    using arena_type = small_ptr_arena<1, BlockSize>;
    using resource_mark_type = typename arena_type::resource_mark_type;

    static arena_type* get() {
        return &arena_;
    }

private:
    static arena_type arena_;
};

template< std::size_t BlockSize > typename small_ptr_arena_factory< BlockSize >::arena_type small_ptr_arena_factory< BlockSize >::arena_;

template <typename T, typename Factory = small_ptr_arena_factory<>, std::size_t MinAlignment = alignof(std::max_align_t) >
class small_ptr_arena_allocator {
    static_assert((MinAlignment & (MinAlignment - 1)) == 0);
    template <typename U, typename FactoryU, std::size_t AlignmentU> friend class small_ptr_arena_allocator;

public:
    using resource_mark_type = typename Factory::resource_mark_type;

    using pointer = small_ptr<T, Factory>;
    using value_type = T;
    static constexpr std::size_t alignment = std::max(MinAlignment, arena_allocator_alignment_v<T>);

    template< typename U > struct rebind {
        using other = small_ptr_arena_allocator<U, Factory, MinAlignment>;
    };

    small_ptr_arena_allocator() = default;
    template <typename U, std::size_t AlignmentU> small_ptr_arena_allocator(const small_ptr_arena_allocator<U, Factory, AlignmentU>&) noexcept
    {}

    pointer allocate(std::size_t n) {
        if (std::numeric_limits<intptr_t>::max() / sizeof(T) < n)
            return 0u;
        return Factory::get()->allocate(sizeof(T) * n, alignment);
    }

    void deallocate(pointer ptr, std::size_t n) noexcept {
        Factory::get()->deallocate(ptr.index_, sizeof(T) * n);
    }

    resource_mark_type resource_mark() { return Factory::get(); }
};

template <typename T, std::size_t AlignmentT, typename U, std::size_t AlignmentU, typename Factory>
bool operator == (const small_ptr_arena_allocator<T, Factory, AlignmentT>&, const small_ptr_arena_allocator<U, Factory, AlignmentU>&) noexcept {
    return true; // TODO
}

template <typename T, std::size_t AlignmentT, typename U, std::size_t AlignmentU, typename Factory>
bool operator != (const small_ptr_arena_allocator<T, Factory, AlignmentT>& x, const small_ptr_arena_allocator<U, Factory, AlignmentU>& y) noexcept {
    return !(x == y);
}

}

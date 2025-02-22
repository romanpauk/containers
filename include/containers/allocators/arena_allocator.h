//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <cassert>
#include <cstdlib>
#include <limits>
#include <memory>

#include <sys/mman.h>

namespace containers {

template< typename T > struct allocator_traits: std::allocator_traits< T > {
    static intptr_t page_size() { return 4096; }
    static intptr_t header_size() { return sizeof(uintptr_t) * 2; }
};

template< typename Arena, typename State > class resource_mark {
    Arena* arena_;
    State state_;

public:
    resource_mark(Arena* arena)
        : arena_(arena)
        , state_(arena->get_state())
    {}

    ~resource_mark() {
        arena_->set_state(state_);
    }

    resource_mark(const resource_mark&) = delete;
    resource_mark(resource_mark&&) = delete;
    resource_mark& operator = (const resource_mark&) = delete;
    resource_mark& operator = (resource_mark&&) = delete;
};

template< typename Allocator = std::allocator<uint8_t> > class arena
    : std::allocator_traits< Allocator >::template rebind_alloc<uint8_t>
{
    using allocator_type = typename allocator_traits< Allocator >::template rebind_alloc<uint8_t>;
    using allocator_traits_type = allocator_traits< allocator_type >;

    static constexpr std::size_t MaxBlockSize = 1<<21;

    struct block {
        block* next;
        uintptr_t size:63;
        uintptr_t owned:1;
    };

    block* block_initial_ = nullptr;
    std::size_t block_size_ = 0;

    struct state {
        block* block_head_ = nullptr;
        intptr_t block_ptr_ = 0;
        intptr_t block_end_ = 0;
        intptr_t allocated_ = 0;
        intptr_t block_size_ = 0;
    };

    state state_;

    bool request_block(intptr_t bytes) {
        assert(state_.block_size_ > 0);

        // For large blocks, glibc's malloc is aligning large allocations
        // to the multiples of page size, also keeping space for chunk size.
        auto header_size = allocator_traits_type::header_size();
        auto page_size = allocator_traits_type::page_size();
        intptr_t size = ((
            header_size +
            std::max<intptr_t>(state_.block_size_, sizeof(block) + bytes) +
            page_size - 1
        ) & ~(page_size - 1)) - header_size;
        if (size < 0)
            return false;
        assert(size - (intptr_t)sizeof(block) >= bytes);
        auto head = allocate_block(size);
        head->size = size;
        head->owned = true;
        push_block(head);
        state_.block_size_ = std::min<intptr_t>(state_.block_size_ * 2, MaxBlockSize);
        return true;
    }

    block* allocate_block(intptr_t size) {
        block *ptr = reinterpret_cast<block*>(allocator_traits_type::allocate(*this, size));
        assert((reinterpret_cast<intptr_t>(ptr) & (alignof(block) - 1)) == 0);
        return ptr;
    }

    void push_block(block* head) {
        head->next = state_.block_head_;
        state_.block_head_ = head;
        state_.block_ptr_ = reinterpret_cast<intptr_t>(head) + sizeof(block);
        state_.block_end_ = reinterpret_cast<intptr_t>(head) + head->size;
    }

    void deallocate_block(block* ptr) {
        assert(ptr->owned);
        allocator_traits_type::deallocate(*this, reinterpret_cast<uint8_t*>(ptr), ptr->size);
    }

    void deallocate_blocks(block* end) {
        auto head = state_.block_head_;
        while(head != end) {
            assert(head->owned || !head->next);
            auto next = head->next;
            if (head->owned)
                deallocate_block(head);
            head = next;
        }
    }

public:
    using resource_mark_type = resource_mark< arena< Allocator >, state >;

    arena(std::size_t block_size)
        : block_size_(block_size)
    {
        state_.block_size_ = block_size;
    }

    template< typename T, std::size_t N > arena(T(&buffer)[N], std::size_t block_size)
        : arena(reinterpret_cast<uint8_t*>(buffer), N * sizeof(T), block_size) {
        static_assert(std::is_trivial_v<T>);
        static_assert(N * sizeof(T) > sizeof(block));
    }

    arena(uint8_t* buffer, std::size_t size, std::size_t block_size)
        : arena(block_size)
    {
        assert(size > sizeof(block));
        auto head = reinterpret_cast<block*>(buffer);
        block_initial_ = head;
        head->owned = false;
        head->size = size;
        push_block(head);
    }

    ~arena() {
        deallocate_blocks(nullptr);
    }

    void* allocate(intptr_t size, intptr_t alignment) {
        assert((alignment & (alignment - 1)) == 0);
        intptr_t padding = -(uintptr_t)state_.block_ptr_ & (alignment - 1);
        intptr_t capacity = state_.block_end_ - state_.block_ptr_ - padding;
        if (capacity < size) {
            if (std::numeric_limits<intptr_t>::max() - (alignment - 1) < size)
                return nullptr;
            if (!request_block(size + (alignment - 1)))
                return nullptr;
            padding = -(uintptr_t)state_.block_ptr_ & (alignment - 1);
            assert(size <= state_.block_end_ - state_.block_ptr_ - padding);
        }
        intptr_t ptr = state_.block_ptr_ + padding;
        assert((ptr & (alignment - 1)) == 0);
        state_.block_ptr_ = ptr + size;
        state_.allocated_ += size;
        return reinterpret_cast<void*>(ptr);
    }

    void deallocate(void*, std::size_t size) {
        state_.allocated_ -= size;
        assert(state_.allocated_ >= 0);
        if (state_.allocated_ == 0) {
            deallocate_blocks(nullptr);
            state_.block_head_ = nullptr;
            state_.block_size_ = block_size_;
            push_block(block_initial_);
        }
    }

    state get_state() const { return state_; }

    void set_state(const state& s) {
        deallocate_blocks(s.block_head_);
        state_ = s;
    }
};

class mmap_arena {
    void* buffer_;
    intptr_t block_size_;

    struct state {
        intptr_t ptr_ = 0;
        intptr_t end_ = 0;
        intptr_t allocated_ = 0;
    };

    state state_;

    static constexpr std::size_t MaxBlockSize = 1<<21;

public:
    using resource_mark_type = resource_mark< mmap_arena, state >;

    mmap_arena(std::size_t size)
        : block_size_(size)
    {}

    ~mmap_arena() {
        if (buffer_)
            munmap(buffer_, block_size_);
    }

    void* allocate(intptr_t size, intptr_t alignment) {
        assert((alignment & (alignment - 1)) == 0);
        intptr_t padding = -(uintptr_t)state_.ptr_ & (alignment - 1);
        intptr_t capacity = state_.end_ - state_.ptr_ - padding;
        if (capacity < size) {
            if (std::numeric_limits<intptr_t>::max() - (alignment - 1) < size)
                return nullptr;

            if (!buffer_) {
                if (block_size_ - padding < size)
                    return nullptr;
                auto buffer = mmap(0, block_size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if (buffer == MAP_FAILED)
                    return nullptr;
                buffer_ = buffer;
            }

            state_.ptr_ = (intptr_t)buffer_;
            state_.end_ = state_.ptr_ + block_size_;
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
            state_.end_ = state_.ptr_ + block_size_;
        }
    }

    state get_state() const { return state_; }

    void set_state(const state& s) {
        state_ = s;
    }
};

template <typename T, typename Arena = arena<> > class arena_allocator {
    template <typename U, typename ArenaU> friend class arena_allocator;
    Arena* arena_ = nullptr;

public:
    using value_type    = T;
    using resource_mark_type = typename Arena::resource_mark_type;

    arena_allocator(Arena& arena) noexcept
        : arena_(&arena) {}

    template <typename U> arena_allocator(const arena_allocator<U, Arena>& other) noexcept
        : arena_(other.arena_) {}

    value_type* allocate(std::size_t n) {
        if (std::numeric_limits<intptr_t>::max() / sizeof(T) < n)
            return nullptr;
        return reinterpret_cast<value_type*>(arena_->allocate(sizeof(T) * n, alignof(T)));
    }

    void deallocate(value_type* ptr, std::size_t n) noexcept {
        arena_->deallocate(ptr, sizeof(T) * n);
    }

    resource_mark_type resource_mark() { return arena_; }
};

template < typename Arena > class arena_allocator<void, Arena> {
    template <typename U, typename ArenaU> friend class arena_allocator;
    Arena* arena_ = nullptr;

public:
    using value_type    = void;
    using resource_mark_type = typename Arena::resource_mark_type;

    arena_allocator(Arena& arena) noexcept
        : arena_(&arena) {}

    template <typename U> arena_allocator(const arena_allocator<U, Arena>& other) noexcept
        : arena_(other.arena_) {}

    resource_mark_type resource_mark() { return arena_; }
};

template <typename T, typename U, typename Arena>
bool operator == (const arena_allocator<T, Arena>& lhs, const arena_allocator<U, Arena>& rhs) noexcept {
    return lhs.arena_ == rhs.arena_;
}

template <typename T, typename U, typename Arena>
bool operator != (const arena_allocator<T, Arena>& x, const arena_allocator<U, Arena>& y) noexcept {
    return !(x == y);
}

}

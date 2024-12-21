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

namespace containers {

template< typename T > struct allocator_traits: std::allocator_traits< T > {
    static intptr_t page_size() { return 4096; }
    static intptr_t header_size() { return sizeof(uintptr_t) * 2; }
};

template< typename Allocator = std::allocator<uint8_t> > class arena
    : std::allocator_traits< Allocator >::template rebind_alloc<uint8_t>
{
    using allocator_type = typename allocator_traits< Allocator >::template rebind_alloc<uint8_t>;
    using allocator_traits_type = allocator_traits< allocator_type >;

    struct block {
        block* next;
        uintptr_t size:63;
        uintptr_t owned:1;
    };

    block* block_head_ = nullptr;
    std::size_t block_size_ = 0;
    intptr_t block_ptr_ = 0;
    intptr_t block_end_ = 0;

    bool request_block(intptr_t bytes) {
        // For large blocks, glibc's malloc is aligning large allocations
        // to the multiples of page size, also keeping space for chunk size.
        auto header_size = allocator_traits_type::header_size();
        auto page_size = allocator_traits_type::page_size();
        intptr_t size = ((
            header_size +
            std::max<intptr_t>(block_size_, sizeof(block) + bytes) +
            page_size - 1
        ) & ~(page_size - 1)) - header_size;
        if (size < 0)
            return false;
        assert(size - (intptr_t)sizeof(block) >= bytes);
        auto head = allocate_block(size);
        head->size = size;
        head->owned = true;
        push_block(head);
        return true;
    }

    block* allocate_block(intptr_t size) {
        block *ptr = reinterpret_cast<block*>(allocator_traits_type::allocate(*this, size));
        assert((reinterpret_cast<intptr_t>(ptr) & (alignof(block) - 1)) == 0);
        return ptr;
    }

    void push_block(block* head) {
        head->next = block_head_;
        block_head_ = head;
        block_ptr_ = reinterpret_cast<intptr_t>(block_head_) + sizeof(block);
        block_end_ = reinterpret_cast<intptr_t>(block_head_) + block_head_->size;
    }

    void deallocate_block(block* ptr) {
        assert(ptr->owned);
        allocator_traits_type::deallocate(*this, reinterpret_cast<uint8_t*>(ptr), ptr->size);
    }

    void deallocate_blocks(block* end) {
        auto head = block_head_;
        while(head != end) {
            assert(head->owned || !head->next);
            auto next = head->next;
            if (head->owned)
                deallocate_block(head);
            head = next;
        }
    }

public:
    class resource_mark {
        arena< Allocator >* arena_;
        block* block_head_;
        intptr_t block_ptr_;
        intptr_t block_end_;

    public:
        resource_mark(arena< Allocator >* arena)
            : arena_(arena)
            , block_head_(arena->block_head_)
            , block_ptr_(arena->block_ptr_)
            , block_end_(arena->block_end_)
        {}

        ~resource_mark() {
            arena_->deallocate_blocks(block_head_);
            arena_->block_head_ = block_head_;
            arena_->block_ptr_ = block_ptr_;
            arena_->block_end_ = block_end_;
        }

        resource_mark(const resource_mark&) = delete;
        resource_mark(resource_mark&&) = delete;
        resource_mark& operator = (const resource_mark&) = delete;
        resource_mark& operator = (resource_mark&&) = delete;
    };

    arena(std::size_t block_size)
        : block_size_(block_size)
    {}

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
        head->owned = false;
        head->size = size;
        push_block(head);
    }

    ~arena() {
        deallocate_blocks(nullptr);
    }

    void* allocate(intptr_t size, intptr_t alignment) {
        assert((alignment & (alignment - 1)) == 0);
        intptr_t padding = -(uintptr_t)block_ptr_ & (alignment - 1);
        intptr_t capacity = block_end_ - block_ptr_ - padding;
        if (capacity < size) {
            if (std::numeric_limits<intptr_t>::max() - (alignment - 1) < size)
                return nullptr;
            if (!request_block(size + (alignment - 1)))
                return nullptr;
            padding = -(uintptr_t)block_ptr_ & (alignment - 1);
            assert(size <= block_end_ - block_ptr_ - padding);
        }
        intptr_t ptr = block_ptr_ + padding;
        assert((ptr & (alignment - 1)) == 0);
        block_ptr_ = ptr + size;
        return reinterpret_cast<void*>(ptr);
    }
};

template <typename T, typename Arena = arena<> > class arena_allocator {
    template <typename U, typename ArenaU> friend class arena_allocator;
    Arena* arena_ = nullptr;

public:
    using value_type    = T;
    using resource_mark_type = typename Arena::resource_mark;

    arena_allocator(Arena& arena) noexcept
        : arena_(&arena) {}

    template <typename U> arena_allocator(const arena_allocator<U, Arena>& other) noexcept
        : arena_(other.arena_) {}

    value_type* allocate(std::size_t n) {
        if (std::numeric_limits<intptr_t>::max() / sizeof(T) < n)
            return nullptr;
        return reinterpret_cast<value_type*>(arena_->allocate(sizeof(T) * n, alignof(T)));
    }

    void deallocate(value_type*, std::size_t) noexcept {}

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

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
#include <stdio.h>

namespace containers {

template< typename Allocator = std::allocator<uint8_t> > class arena
    : std::allocator_traits< Allocator >::template rebind_alloc<uint8_t>
{
    using allocator_type = typename std::allocator_traits< Allocator >::template rebind_alloc<uint8_t>;
    using allocator_traits = std::allocator_traits< allocator_type >;

    struct block {
        block* next;
        std::size_t size;
        bool owned;
    };

    std::size_t block_size_;
    block* block_ = nullptr;
    uintptr_t block_ptr_ = 0;
    uintptr_t block_end_ = 0;

    bool request_block(std::size_t bytes) {
        if (std::numeric_limits<std::size_t>::max() - sizeof(block) < bytes)
            return false;
        std::size_t size = std::max(block_size_, sizeof(block) + bytes);
        assert(size - sizeof(block) >= bytes);
        auto head = allocate_block(size);
        head->owned = true;
        head->size = size;
        push_block(head);
        return true;
    }

    block* allocate_block(std::size_t size) {
        block *ptr = reinterpret_cast<block*>(allocator_traits::allocate(*this, size));
        assert((reinterpret_cast<uintptr_t>(ptr) & (alignof(block) - 1)) == 0);
        return ptr;
    }

    void push_block(block* head) {
        head->next = block_;
        block_ = head;
        block_ptr_ = reinterpret_cast<uintptr_t>(block_) + sizeof(block);
        block_end_ = reinterpret_cast<uintptr_t>(block_) + block_->size;
    }

    void deallocate_block(block* ptr) {
        assert(ptr->owned);
        allocator_traits::deallocate(*this, reinterpret_cast<uint8_t*>(ptr), ptr->size);
    }

public:
    arena(std::size_t block_size)
        : block_size_(block_size)
    {}

    template< typename T, std::size_t N > arena(T(&buffer)[N], std::size_t block_size)
        : arena(reinterpret_cast<uint8_t*>(buffer), N * sizeof(T), block_size) {
        static_assert(std::is_trivial_v<T>);
        static_assert(N * sizeof(T) > sizeof(block));
    }

    arena(uint8_t* buffer, std::size_t size, std::size_t block_size_default)
        : arena(block_size_default)
    {
        assert(size > sizeof(block));
        auto head = reinterpret_cast<block*>(buffer);
        head->owned = false;
        head->size = size;
        push_block(head);
    }

    ~arena() {
        auto head = block_;
        while(head) {
            assert(head->owned || !head->next);
            auto next = head->next;
            if (head->owned)
                deallocate_block(head);
            head = next;
        }
    }

    void* allocate(std::size_t size, std::size_t alignment) {
        assert(alignment);
        assert((alignment & (alignment - 1)) == 0);
        std::size_t capacity = block_end_ - block_ptr_ - (alignment - 1);
        if (capacity < size) {
            if (std::numeric_limits<std::size_t>::max() - (alignment - 1) < size)
                return nullptr;
            if (!request_block(size + (alignment - 1)))
                return nullptr;
            assert(size <= block_end_ - block_ptr_ - (alignment - 1));
        }
        uintptr_t ptr = (block_ptr_ + alignment - 1) & ~(alignment - 1);
        block_ptr_ = ptr + size;
        assert((ptr & (alignment - 1)) == 0);
        return reinterpret_cast<void*>(ptr);
    }
};

template <typename T, typename Arena = arena<> > class arena_allocator {
    template <typename U, typename ArenaU> friend class arena_allocator;
    Arena* arena_ = nullptr;

public:
    using value_type    = T;

    arena_allocator(Arena& arena) noexcept
        : arena_(&arena) {}

    template <typename U> arena_allocator(const arena_allocator<U, Arena>& other) noexcept
        : arena_(other.arena_) {}

    value_type* allocate(std::size_t n) {
        if (std::numeric_limits<std::size_t>::max() / sizeof(T) < n)
            return nullptr;
        return reinterpret_cast<value_type*>(arena_->allocate(sizeof(T) * n, alignof(T)));
    }

    void deallocate(value_type*, std::size_t) noexcept {}
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

//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <containers/allocators/arena_allocator.h>

namespace containers {

template< std::size_t BlockSize > struct thread_arena_factory {
    using arena_type = arena<>;
    using resource_mark_type = arena_type::resource_mark_type;

    static arena_type* get() {
        static thread_local uint8_t buffer[BlockSize];
        static thread_local arena_type arena(buffer, BlockSize);
        return &arena;
    }
};

struct thread_mmap_arena_factory {
    using arena_type = mmap_arena;
    using resource_mark_type = arena_type::resource_mark_type;

    static arena_type* get() {
        static thread_local arena_type arena(1<<30);
        return &arena;
    }
};

template <typename T, typename ArenaFactory = thread_arena_factory< 1<<16 >, std::size_t MinAlignment = alignof(std::max_align_t) > class thread_arena_allocator {
    static_assert((MinAlignment & (MinAlignment - 1)) == 0);
public:
    using value_type    = T;
    using resource_mark_type = typename ArenaFactory::resource_mark_type;
    static constexpr std::size_t alignment = std::max(MinAlignment, arena_allocator_alignment_v<T>);

    template < typename U > struct rebind {
        using other = thread_arena_allocator< U, ArenaFactory, MinAlignment >;
    };

    thread_arena_allocator() = default;
    template <typename U, std::size_t AlignmentU> thread_arena_allocator(const thread_arena_allocator<U, ArenaFactory, AlignmentU>&) noexcept {}

    value_type* allocate(std::size_t n) {
        if (std::numeric_limits<intptr_t>::max() / sizeof(T) < n)
            return nullptr;
        return reinterpret_cast<value_type*>(ArenaFactory::get()->allocate(sizeof(T) * n, alignment));
    }

    void deallocate(value_type* ptr, std::size_t n) noexcept {
        ArenaFactory::get()->deallocate(ptr, sizeof(T)*n);
    }

    static resource_mark_type resource_mark() { return ArenaFactory::get(); }
};

template <typename ArenaFactory, std::size_t MinAlignment> class thread_arena_allocator<void, ArenaFactory, MinAlignment> {
public:
    using resource_mark_type = typename ArenaFactory::resource_mark_type;

    template < typename U > struct rebind {
        using other = thread_arena_allocator< U, ArenaFactory, MinAlignment >;
    };

    thread_arena_allocator() = default;
    template <typename U, std::size_t AlignmentU> thread_arena_allocator(const thread_arena_allocator<U, ArenaFactory, AlignmentU>&) noexcept {}

    static resource_mark_type resource_mark() { return ArenaFactory::get(); }
};


template <typename T, std::size_t AlignmentT, typename U, std::size_t AlignmentU, typename ArenaFactory>
bool operator == (const thread_arena_allocator<T, ArenaFactory, AlignmentT>&, const thread_arena_allocator<U, ArenaFactory, AlignmentU>&) noexcept {
    return true;
}

template <typename T, std::size_t AlignmentT, typename U, std::size_t AlignmentU, typename ArenaFactory>
bool operator != (const thread_arena_allocator<T, ArenaFactory, AlignmentT>& x, const thread_arena_allocator<U, ArenaFactory, AlignmentU>& y) noexcept {
    return !(x == y);
}

}


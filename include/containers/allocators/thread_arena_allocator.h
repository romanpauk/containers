//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <containers/allocators/arena_allocator.h>

namespace containers {

template <typename Arena, std::size_t Size> struct thread_arena {
    static Arena* get() {
        static thread_local Arena arena(Size);
        return &arena;
    }
};

template <typename T, typename Arena = arena<> > class thread_arena_allocator {
public:
    using value_type    = T;
    using resource_mark_type = typename Arena::resource_mark;
    using arena_type = thread_arena<Arena, 1<<20>;

    value_type* allocate(std::size_t n) {
        if (std::numeric_limits<intptr_t>::max() / sizeof(T) < n)
            return nullptr;
        return reinterpret_cast<value_type*>(arena_type::get()->allocate(sizeof(T) * n, alignof(T)));
    }

    void deallocate(value_type*, std::size_t) noexcept {}

    resource_mark_type resource_mark() { return arena_type::get(); }
};

template <typename T, typename U, typename Arena>
bool operator == (const thread_arena_allocator<T, Arena>&, const thread_arena_allocator<U, Arena>&) noexcept {
    return true;
}

template <typename T, typename U, typename Arena>
bool operator != (const thread_arena_allocator<T, Arena>& x, const thread_arena_allocator<U, Arena>& y) noexcept {
    return !(x == y);
}


}


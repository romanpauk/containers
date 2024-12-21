//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <containers/allocators/arena_allocator.h>

namespace containers {

struct thread_arena_factory {
    using resource_mark_type = arena<>::resource_mark;

    static arena<>* get() {
        static thread_local uint8_t buffer[1<<10];
        static thread_local arena<> arena(buffer, 1<<16);
        return &arena;
    }
};

template <typename T, typename ArenaFactory = thread_arena_factory > class thread_arena_allocator {
public:
    using value_type    = T;
    using resource_mark_type = typename ArenaFactory::resource_mark_type;

    thread_arena_allocator() = default;
    template <typename U> thread_arena_allocator(const thread_arena_allocator<U, ArenaFactory>&) noexcept {}

    value_type* allocate(std::size_t n) {
        if (std::numeric_limits<intptr_t>::max() / sizeof(T) < n)
            return nullptr;
        return reinterpret_cast<value_type*>(ArenaFactory::get()->allocate(sizeof(T) * n, alignof(T)));
    }

    void deallocate(value_type*, std::size_t) noexcept {}

    static resource_mark_type resource_mark() { return ArenaFactory::get(); }
};

template <typename T, typename U, typename ArenaFactory>
bool operator == (const thread_arena_allocator<T, ArenaFactory>&, const thread_arena_allocator<U, ArenaFactory>&) noexcept {
    return true;
}

template <typename T, typename U, typename ArenaFactory>
bool operator != (const thread_arena_allocator<T, ArenaFactory>& x, const thread_arena_allocator<U, ArenaFactory>& y) noexcept {
    return !(x == y);
}


}


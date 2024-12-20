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
    static Arena& get() {
        static thread_local Arena arena(Size);
        return arena;
    }
};

template <typename T, typename Arena = arena<> > class thread_arena_allocator
    : public arena_allocator<T, Arena >
{
    template <typename U, typename ArenaU> friend class thread_arena_allocator;

public:
    thread_arena_allocator() noexcept
        : arena_allocator< T, Arena >(thread_arena<Arena, 1<<20>::get()) {}
};

}


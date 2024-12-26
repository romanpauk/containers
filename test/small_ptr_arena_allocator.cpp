//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/allocators/small_ptr_arena_allocator.h>

#include <gtest/gtest.h>

TEST(small_ptr_arena_allocator_test, test) {
    containers::small_ptr_mmap_arena arena(1<<16);
    containers::small_ptr_arena_allocator<int> allocator(arena);
}



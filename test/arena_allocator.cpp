//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/allocators/arena_allocator.h>
#include <containers/allocators/page_allocator.h>

#include <gtest/gtest.h>

TEST(arena_allocator_test, std_allocator) {
    uint8_t buffer[128];
    containers::arena<> arena(buffer, 1<<20);
    containers::arena_allocator< char > allocator(arena);

    allocator.allocate(128);
}

TEST(arena_allocator_test, page_allocator) {
    uint8_t buffer[128];
    containers::arena< containers::page_allocator<char> > arena(buffer, sizeof(buffer), 1<<20);
    containers::arena_allocator< char, decltype(arena) > allocator(arena);

    allocator.allocate(128);
}

TEST(arena_allocator_test, resource_mark) {
    uint8_t buffer[128];
    containers::arena< containers::page_allocator<char> > arena(buffer, sizeof(buffer), 1<<20);
    containers::arena_allocator< char, decltype(arena) > allocator(arena);

    auto alloc = [&]{
        auto mark = allocator.resource_mark();
        return allocator.allocate(128);
    };

    ASSERT_EQ(alloc(), alloc());
}

TEST(arena_allocator_test, void_allocator) {
    containers::arena<> arena(1<<20);
    containers::arena_allocator< char > allocator1(arena);
    containers::arena_allocator< void > void_allocator(allocator1);
    containers::arena_allocator< char > allocator2(void_allocator);

    // TODO: test operator ==
}



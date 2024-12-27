//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/allocators/small_ptr_arena_allocator.h>

#include <deque>
#include <list>
#include <set>
#include <vector>

#include <gtest/gtest.h>

TEST(small_ptr_arena_allocator_test, pointer_test) {
    containers::small_ptr_arena_allocator<int> allocator;
    auto rm = allocator.resource_mark();
    auto ptr = allocator.allocate(2);
    *ptr++ = 1;
    *ptr++ = 2;
    ASSERT_EQ(*(ptr - 1), 2);
    ASSERT_EQ(*(ptr - 2), 1);

    int* p = &*(ptr - 2);
    ASSERT_EQ(*p, 1);
    ASSERT_EQ(*(p + 1), 2);
}

TEST(small_ptr_arena_allocator_test, test_vector_sizeof) {
    static_assert(sizeof(std::vector<int>) == 24);
    static_assert(sizeof(std::vector<int, containers::small_ptr_arena_allocator<int>>) == 12);
}

TEST(small_ptr_arena_allocator_test, test_deque_sizeof) {
    static_assert(sizeof(std::deque<int>) == 80);
    static_assert(sizeof(std::deque<int, containers::small_ptr_arena_allocator<int>>) == 48);
}

// Bug 57272 - node-based containers don't use allocator's pointer type internally
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=57272
TEST(small_ptr_arena_allocator_test, test_list_sizeof) {
    static_assert(sizeof(std::list<int>) == 24);
    static_assert(sizeof(std::list<int, containers::small_ptr_arena_allocator<int>>) == 24);
}

TEST(small_ptr_arena_allocator_test, test_set_sizeof) {
    static_assert(sizeof(std::set<int>) == 48);
    static_assert(sizeof(std::set<int, std::less<int>, containers::small_ptr_arena_allocator<int>>) == 48);
}

TEST(small_ptr_arena_allocator_test, test_vector) {
    std::vector<int, containers::small_ptr_arena_allocator<int>> vector;
    vector.emplace_back(10);
    vector.clear();
}

TEST(small_ptr_arena_allocator_test, test_set) {
    std::set<int, std::less<int>, containers::small_ptr_arena_allocator<int>> set;
    set.insert(1);
    set.erase(1);
}


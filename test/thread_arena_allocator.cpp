//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/allocators/thread_arena_allocator.h>

#include <vector>
#include <map>
#include <unordered_map>

#include <gtest/gtest.h>

template< typename T > using arena_vector = std::vector< T, containers::thread_arena_allocator<T> >;
template< typename K, typename V > using arena_map = std::map< K, V, std::less<K>, containers::thread_arena_allocator<std::pair<const K, V>> >;
template< typename K, typename V > using arena_unordered_map = std::unordered_map< K, V, std::hash<K>, std::equal_to<K>, containers::thread_arena_allocator<std::pair<const K, V>> >;

TEST(thread_arena_allocator_test, allocator) {
    containers::thread_arena_allocator< char > allocator;
    {
        auto mark = allocator.resource_mark();
        allocator.allocate(128);
    }

    {
        auto mark = allocator.resource_mark();
        arena_vector<int> vec;
        for (int i = 0; i < 10000; ++i)
            vec.emplace_back(i);
    }

    {
        auto mark = allocator.resource_mark();
        arena_map<int, int> map;
        for (int i = 0; i < 10000; ++i)
            map[i] = i;
    }

    {
        auto mark = allocator.resource_mark();
        arena_unordered_map<int, int> map;
        for (int i = 0; i < 10000; ++i)
            map[i] = i;
    }
}


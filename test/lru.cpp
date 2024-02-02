//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/cache/lru.h>

#include <gtest/gtest.h>

TEST(lru, basic_operations) {

    containers::lru_unordered_map< int, int > cache;
    ASSERT_EQ(cache.evictables().begin(), cache.evictables().end());
    cache.emplace(1, 100);
    ASSERT_EQ(cache.evictables().begin()->first, 1);
    cache.emplace(2, 200);
    ASSERT_EQ(cache.evictables().begin()->first, 1);
    cache.emplace(3, 300);
    ASSERT_EQ(cache.evictables().begin()->first, 1);
    cache.touch(cache.find(1));
    ASSERT_EQ(cache.evictables().begin()->first, 2);
    cache.erase(cache.evictables().begin());
    ASSERT_EQ(cache.evictables().begin()->first, 3);
    cache.erase(cache.evictables().begin());
    ASSERT_EQ(cache.evictables().begin()->first, 1);
}

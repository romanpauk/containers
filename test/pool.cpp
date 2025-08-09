//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/allocators/pool.h>

#include <gtest/gtest.h>

TEST(pool_allocator, basics) {
    containers::PageGroupManager< 1ull<<31 > manager;
    containers::pool_allocator<uint64_t, decltype(manager) > pool(manager);
    pool.allocate(1);
}



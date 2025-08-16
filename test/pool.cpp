//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/allocators/pool.h>

#include <gtest/gtest.h>

TEST(pool_allocator, sizes) {
    containers::GlobalPageGroupManager< 1ull<<32 > manager;
    containers::pool_allocator<uint64_t, decltype(manager) > pool(manager);

    std::cerr << "sizeof(PageGroup) = " << decltype(manager)::PageGroupSize << std::endl;
    std::cerr << "sizeof(PageGroupDescriptor) = " << sizeof(containers::PageGroupDescriptor) << ", ratio " <<
        (double)sizeof(containers::PageGroupDescriptor) / decltype(manager)::PageGroupSize << std::endl;
    std::cerr << "sizeof(PageGroupManager) = " << sizeof(decltype(manager)) << std::endl;

}

TEST(pool_allocator, basics) {
    containers::GlobalPageGroupManager< 1ull<<32 > manager;
    containers::pool_allocator<uint64_t, decltype(manager) > pool(manager);

    std::vector<uint64_t*> ptrs(10000);
    for(size_t i = 0; i < ptrs.size(); ++i) {
        ptrs[i] = pool.allocate(1);
    }

    for(size_t i = 0; i < ptrs.size(); ++i) {
        pool.deallocate(ptrs[i], 1);
    }
}

TEST(bitmap, tzcnt) {
    containers::bitmap<64> bitmap(0);
    ASSERT_EQ(bitmap.tzcnt(), bitmap.size());
    bitmap.set_bit(1);
    ASSERT_EQ(bitmap.tzcnt(), 1);
    bitmap.set_bit(0);
    ASSERT_EQ(bitmap.tzcnt(), 0);
}

TEST(bitmap_large, tzcnt) {
    containers::bitmap<1024> bitmap(0);
    for (std::size_t i = 0; i < bitmap.size(); ++i) {
        ASSERT_EQ(bitmap.get_bit(i), 0);
        ASSERT_EQ(bitmap.ffz(), i);
        ASSERT_EQ(bitmap.popcnt(), i);
        bitmap.set_bit(i);
        ASSERT_EQ(bitmap.get_bit(i), 1);
    }
}

TEST(bitmap, ffz) {
    containers::bitmap<64> bitmap(0);
    ASSERT_EQ(bitmap.ffz(), 0);
    bitmap.set_bit(1);
    ASSERT_EQ(bitmap.ffz(), 0);
    bitmap.set_bit(0);
    ASSERT_EQ(bitmap.ffz(), 2);
    bitmap.set(-1);
    ASSERT_EQ(bitmap.ffz(), bitmap.size());
}


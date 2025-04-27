//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/allocators/vbr.h>
#include <containers/queue.hpp>

#include <thread>

#include <gtest/gtest.h>

struct node_ptr {
    uint64_t version;
    uint64_t value;
};

struct queue_node {
    node_ptr next;
    uint64_t value;
};

TEST(vbr, page_allocator) {
    containers::vbr_allocator<queue_node, 1<<30> allocator;
    auto* p = allocator.page_allocator_.allocate_page();
    assert(p->state() & (int)containers::PageState::Active);
    allocator.page_allocator_.decommit_page(p);
    assert(p->state() == (int)containers::PageState::Decommitted);
    auto* p2 = allocator.page_allocator_.allocate_page();
    assert(p == p2);
    assert(p2->state() & (int)containers::PageState::Active);
}

TEST(vbr, allocate) {
    containers::vbr_allocator<queue_node, 1<<30> allocator;

    for(int i = 0; i < 10000; ++i) {
        auto node = allocator.allocate();
        allocator.deallocate(node);
    }
}

TEST(vbr, allocate_ptr) {
    containers::vbr_allocator<queue_node, 1<<30> allocator;

    for(int i = 0; i < 10000; ++i) {
        auto ptr = allocator.allocate();
        auto* p = allocator.read(ptr);
        allocator.deallocate(ptr);
    }
}

TEST(vbr, queue) {
    int N = 100000;
    containers::queue<int> q;
    for (int i = 0; i < N; ++i) {
        q.emplace(i);
    }

    for (int i = 0; i < N; ++i) {
        int val;
        ASSERT_TRUE(q.pop(val));
        ASSERT_EQ(val, i);
    }
}

TEST(vbr, queue_parallel) {
    int N = 1000000;
    containers::queue<int> q;
    std::thread producer([&]{
        for (int i = 0; i < N; ++i) {
            q.emplace(i);
        }
    });

    std::thread consumer([&]{
        for (int i = 0; i < N; ++i) {
            int val;
            while(!q.pop(val))
                ;
            ASSERT_EQ(val, i);
        }
    });

    producer.join();
    consumer.join();
}



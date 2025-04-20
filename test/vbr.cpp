//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/allocators/vbr.h>

#include <gtest/gtest.h>

TEST(vbr, basic_operations) {
    struct node_ptr {
        uint64_t version;
        uint64_t value;
    };

    struct queue_node {
        node_ptr next;
        uint64_t value;
    };

    containers::vbr_allocator<queue_node, 1<<30> allocator;
    auto* p = allocator.page_allocator_.allocate_page();
    assert(p->state() & (int)containers::PageState::Active);
    allocator.page_allocator_.decommit_page(p);
    assert(p->state() == (int)containers::PageState::Decommitted);
    auto* p2 = allocator.page_allocator_.allocate_page();
    assert(p == p2);
    assert(p2->state() & (int)containers::PageState::Active);
}


//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/allocators/vbr.h>

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

 //
// Simple, fast, and practical non-blocking and blocking concurrent queue algorithms.
// http://www.cs.rochester.edu/~scott/papers/1996_PODC_queues.pdf
//
template < typename T > class queue {
    struct node {
        node() = default;
        node(containers::vbr_ptr<node> n, T&& v)
            : next(n), value(std::move(v))
        {}

        std::atomic< containers::vbr_ptr<node> > next;
        T value;
    };

    using allocator_type = containers::vbr_allocator<node, 1<<30>;

    allocator_type allocator_;
    alignas(64) std::atomic< containers::vbr_ptr<node> > head_;
    alignas(64) std::atomic< containers::vbr_ptr<node> > tail_;

public:
    using value_type = T;

    queue() {
        auto n = allocator_.allocate();
        allocator_.construct(n);
        head_.store(n);
        tail_.store(n);
    }

    ~queue() {
        clear();
    }

    template< typename... Args > void emplace(Args&&... args) {
        auto n = allocator_.allocate();
        allocator_.construct(n, nullptr, std::move(T{std::forward< Args >(args)...}));
        // Backoff backoff;
        while (true) {
            auto tail = tail_.load(std::memory_order_relaxed);
            auto next = allocator_.read(tail)->next.load(std::memory_order_relaxed);
            if (tail == tail_.load(std::memory_order_acquire)) {
                if (next == nullptr) {
                    if (allocator_.read(tail)->next.compare_exchange_weak(next, n, std::memory_order_relaxed)) {
                        tail_.compare_exchange_weak(tail, n, std::memory_order_release);
                        break;
                    }
                } else {
                    tail_.compare_exchange_weak(tail, next, std::memory_order_relaxed);
                }
            }

            //backoff();
        }
    }

    void push(const T& value) { return emplace(value); }
    void push(T&& value) { return emplace(std::move(value)); }

    bool pop(T& value) {
        //Backoff backoff;
        while (true) {
            auto head = head_.load(std::memory_order_relaxed);
            auto tail = tail_.load(std::memory_order_relaxed);
            auto next = allocator_.read(head)->next.load(std::memory_order_relaxed);
            if (head == head_.load(std::memory_order_acquire)) {
                if (head == tail) {
                    if (next == nullptr)
                        return false;
                    tail_.compare_exchange_weak(tail, next, std::memory_order_relaxed);
                } else {
                    value = allocator_.read(next)->value;
                    if (head_.compare_exchange_weak(head, next, std::memory_order_release)) {
                        allocator_.destroy(head);
                        allocator_.deallocate(head);
                        return true;
                    }
                }
            }

            //backoff();
        }
    }

    bool empty() const {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

private:
    void clear() {
        std::atomic_thread_fence(std::memory_order_acquire);
        auto head = head_.load(std::memory_order_relaxed);
        while (head) {
            auto next = allocator_.read(head)->next.load(std::memory_order_relaxed);
            allocator_.destroy(head);
            allocator_.deallocate(head);
            head = next;
        }
    }
};

TEST(vbr, queue) {
    int N = 100000;
    queue<int> q;
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
    queue<int> q;
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



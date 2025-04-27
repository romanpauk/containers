//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <containers/allocators/vbr.h>

namespace containers {

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

}

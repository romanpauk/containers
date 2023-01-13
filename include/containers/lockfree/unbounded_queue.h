//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <containers/lockfree/detail/exponential_backoff.h>
#include <containers/lockfree/detail/hazard_era_allocator.h>

#include <atomic>
#include <memory>

namespace containers
{
    //
    // Simple, fast, and practical non-blocking and blocking concurrent queue algorithms.
    // http://www.cs.rochester.edu/~scott/papers/1996_PODC_queues.pdf
    //
    template <
        typename T,
        typename Allocator = hazard_era_allocator< T >,
        typename Backoff = exponential_backoff<>
    > class unbounded_queue
    {
        struct node
        {
            std::atomic< node* > next{};
            T value;
        };

        using allocator_type = typename Allocator::template rebind< node >::other;
        allocator_type& allocator_;

        alignas(64) std::atomic< node* > head_;
        alignas(64) std::atomic< node* > tail_;

    public:
        using value_type = T;

        unbounded_queue(Allocator& allocator = Allocator::instance())
            : allocator_(*reinterpret_cast<allocator_type*>(&allocator))
        {
            auto n = allocator_.allocate();
            head_.store(n, std::memory_order_relaxed);
            tail_.store(n, std::memory_order_relaxed);
        }

        ~unbounded_queue()
        {
            clear();
        }

        template< typename... Args > void emplace(Args&&... args)
        {
            auto guard = allocator_.guard();
            auto n = allocator_.allocate(nullptr, T{std::forward< Args >(args)...});
            Backoff backoff;
            while (true)
            {
                // TODO: could this benefit from protecting multiple variables in one call?
                auto tail = allocator_.protect(tail_, std::memory_order_relaxed);
                auto next = allocator_.protect(tail->next, std::memory_order_relaxed);
                if (tail == tail_.load(std::memory_order_relaxed))
                {
                    if (next == nullptr)
                    {
                        if (tail->next.compare_exchange_weak(next, n, std::memory_order_relaxed))
                        {
                            tail_.compare_exchange_weak(tail, n, std::memory_order_release);
                            break;
                        }
                    }
                    else
                    {
                        tail_.compare_exchange_weak(tail, next, std::memory_order_relaxed);
                    }
                }

                backoff();
            }
        }

        void push(const T& value) { return emplace(value); }
        void push(T&& value) { return emplace(std::move(value)); }

        bool pop(T& value)
        {
            auto guard = allocator_.guard();
            Backoff backoff;
            while (true)
            {
                auto head = allocator_.protect(head_, std::memory_order_relaxed);
                auto next = allocator_.protect(head->next, std::memory_order_relaxed);
                auto tail = tail_.load(std::memory_order_relaxed);
                if (head == head_.load(std::memory_order_relaxed))
                {
                    if (head == tail)
                    {
                        if (next == nullptr)
                            return false;

                        tail_.compare_exchange_weak(tail, next, std::memory_order_relaxed);
                    }
                    else
                    {
                        std::atomic_thread_fence(std::memory_order_acquire);
                        value = next->value;

                        if (head_.compare_exchange_weak(head, next, std::memory_order_release))
                        {
                            allocator_.retire(head);
                            return true;
                        }
                    }
                }

                backoff();
            }
        }

        bool empty() const
        {
            return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
        }

    private:
        void clear()
        {
            auto head = head_.load(std::memory_order_acquire);
            while (head)
            {
                auto next = head->next.load(std::memory_order_relaxed);
                allocator_.deallocate(head);
                head = next;
            }
        }
    };
}

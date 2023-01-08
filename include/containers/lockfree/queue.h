//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <containers/lockfree/detail/hazard_era_allocator.h>

#include <atomic>
#include <memory>

namespace containers
{
    // Simple, fast, and practical non-blocking and blocking concurrent queue algorithms.
    // http://www.cs.rochester.edu/~scott/papers/1996_PODC_queues.pdf
    template < typename T, typename Allocator = hazard_era_allocator< T >, typename Backoff = exp_backoff<> > class unbounded_queue
    {
        struct queue_node
        {
            T value;
            std::atomic< queue_node* > next;
        };

        using allocator_type = typename Allocator::template rebind< queue_node >::other;
        allocator_type& allocator_;
        
        alignas(64) std::atomic< queue_node* > head_;
        alignas(64) std::atomic< queue_node* > tail_;

    public:
        unbounded_queue(Allocator& allocator = Allocator::instance())
            : allocator_(*reinterpret_cast<allocator_type*>(&allocator))
        {
            auto n = allocator_.allocate();
            n->next = nullptr;
            head_.store(n, std::memory_order_relaxed);
            tail_.store(n, std::memory_order_relaxed);
        }

        ~unbounded_queue()
        {
            clear();
        }
        
        template< typename Ty > void push(Ty&& value)
        {
            auto guard = allocator_.guard();
            auto n = allocator_.allocate(std::forward< Ty >(value), nullptr);
            Backoff backoff;
            while(true)
            {
                // TODO: could this benefit from protecting multiple variables in one call?
                auto tail = allocator_.protect(tail_, std::memory_order_relaxed);
                auto next = allocator_.protect(tail->next, std::memory_order_relaxed);
                if (tail == tail_.load())
                {
                    if (next == nullptr)
                    {
                        if (tail->next.compare_exchange_weak(next, n))
                        {
                            tail_.compare_exchange_weak(tail, n);
                            break;
                        }
                        else
                            backoff();
                    }
                    else
                    {
                        tail_.compare_exchange_weak(tail, next);
                    }
                }
            }
        }

        bool pop(T& value)
        {
            auto guard = allocator_.guard();
            Backoff backoff;
            while (true)
            {
                auto head = allocator_.protect(head_, std::memory_order_relaxed);
                auto next = allocator_.protect(head->next, std::memory_order_relaxed);
                auto tail = tail_.load();
                if (head == head_.load())
                {
                    if (head == tail)
                    {
                        if(next == nullptr)
                            return false;

                        tail_.compare_exchange_weak(tail, next);
                    }
                    else
                    {
                        value = next->value;
                        if (head_.compare_exchange_weak(head, next))
                        {
                            allocator_.retire(head);
                            return true;
                        }
                        else
                            backoff();
                    }
                }
            }
        }

    private:
        void clear()
        {
            auto head = head_.load();
            while (head)
            {
                auto next = head->next.load();
                allocator_.deallocate_unsafe(head);
                head = next;
            }
        }
    };

    template< typename T, size_t Size, typename Backoff = exp_backoff<> > class bounded_queue
    {
        alignas(64) std::atomic< size_t > chead_;
        std::atomic< size_t > ctail_;

        alignas(64) std::atomic< size_t > phead_;
        std::atomic< size_t > ptail_;

        static_assert(is_power_of_2<Size>::value);
        alignas(64) std::array< T, Size > values_;

    public:
        template< typename Ty > bool push(Ty&& value)
        {
            Backoff backoff;
            while (true)
            {
                auto ph = phead_.load(std::memory_order_acquire);
                auto pn = ph + 1;
                if (pn > ctail_.load(std::memory_order_acquire) + Size)
                    return false;
                if (!phead_.compare_exchange_strong(ph, pn))
                {
                    backoff();
                }
                else
                {
                    values_[pn & (Size - 1)] = std::forward< Ty >(value);
                    while (ptail_.load(std::memory_order_acquire) != ph)
                        _mm_pause();
                    ptail_.store(pn, std::memory_order_release);
                    return true;
                }
            }
        }

        bool pop(T& value)
        {
            Backoff backoff;
            while (true)
            {
                auto ch = chead_.load(std::memory_order_acquire);
                auto cn = ch + 1;
                if (cn > ptail_.load(std::memory_order_acquire) + 1)
                    return false;
                if (!chead_.compare_exchange_strong(ch, cn))
                {
                    backoff();
                }
                else
                {
                    value = std::move(values_[cn & (Size - 1)]);
                    while (ctail_.load(std::memory_order_acquire) != ch)
                        _mm_pause();
                    ctail_.store(cn, std::memory_order_release);
                    return true;
                }
            }
        }
    };

    // A Scalable, Portable, and Memory-Efficient Lock-Free FIFO Queue - https://arxiv.org/abs/1908.04511
    // BBQ: A Block-based Bounded Queue - https://www.usenix.org/conference/atc22/presentation/wang-jiawei
}

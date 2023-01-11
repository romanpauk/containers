//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <containers/lockfree/detail/exponential_backoff.h>
#include <containers/lockfree/detail/hazard_era_allocator.h>
#include <containers/lockfree/bounded_stack.h>

#include <atomic>
#include <memory>

namespace containers
{
    //
    // https://en.wikipedia.org/wiki/Treiber_stack
    //
    template<
        typename T,
        typename Allocator = hazard_era_allocator< T >,
        typename Backoff = exponential_backoff<>
    > class unbounded_stack
    {
        struct stack_node
        {
            T value;
            stack_node* next;
        };

        using allocator_type = typename Allocator::template rebind< stack_node >::other;
        allocator_type& allocator_;

        alignas(64) std::atomic< stack_node* > head_;

    public:
        unbounded_stack(Allocator& allocator = Allocator::instance())
            : allocator_(*reinterpret_cast<allocator_type*>(&allocator))
        {}

        ~unbounded_stack()
        {
            clear();
        }

        template< typename Ty > void push(Ty&& value)
        {
            auto head = allocator_.allocate(std::forward< Ty >(value), head_.load(std::memory_order_relaxed));
            Backoff backoff;
            while (!head_.compare_exchange_weak(head->next, head, std::memory_order_release))
                backoff();
        }

        bool pop(T& value)
        {
            Backoff backoff;
            auto guard = allocator_.guard();
            while (true)
            {
                auto head = allocator_.protect(head_, std::memory_order_relaxed);
                if (!head)
                {
                    return false;
                }

                if (head_.compare_exchange_weak(head, head->next, std::memory_order_release))
                {
                    std::atomic_thread_fence(std::memory_order_acquire);
                    value = std::move(head->value);
                    allocator_.retire(head);
                    return true;
                }
                else
                    backoff();
            }
        }

    private:
        void clear()
        {
            auto head = head_.load(std::memory_order_acquire);
            while (head)
            {
                auto next = head->next;
                allocator_.deallocate_unsafe(head);
                head = next;
            }
        }
    };

    //
    // This algorithm just marks a block for deletion. If either pop or push observe failure
    // when working with the block, them mark it. Whoever sees marked block, tries to remove it.
    // So all are working on sequence of operations (block fine -> marked -> removed).
    //
    // With N=128, we can run hazard_era_reclamation with every allocation/deallocation
    // without performance impact.
    //
    template<
        typename T,
        typename Allocator = hazard_era_allocator< T >,
        typename Backoff = exponential_backoff<>,
        typename InnerStack = bounded_stack_base< T, 128, Backoff, -1 >
    > class unbounded_blocked_stack
    {
        struct node
        {
            node* next{};
            InnerStack stack;
        };

        using allocator_type = typename Allocator::template rebind< node >::other;
        allocator_type& allocator_;

        alignas(64) std::atomic< node* > head_{};

    public:
        unbounded_blocked_stack(Allocator& allocator = Allocator::instance())
            : allocator_(*reinterpret_cast<allocator_type*>(&allocator))
        {
            head_ = allocator_.allocate(nullptr);
        }

        ~unbounded_blocked_stack()
        {
            clear();
        }

        template< typename Ty > void push(Ty&& value)
        {
            auto guard = allocator_.guard();
            while (true)
            {
                auto head = allocator_.protect(head_, std::memory_order_relaxed);
                auto top = head->stack.top_.load(std::memory_order_relaxed);
                if (head->stack.push(std::forward< Ty >(value)))
                    return;

                if (top.index == -1 && head_.compare_exchange_weak(head, head->next, std::memory_order_release))
                {
                    std::atomic_thread_fence(std::memory_order_acquire);
                    allocator_.retire(head);
                }
                else
                {
                    head = allocator_.allocate(nullptr);
                    if (!head_.compare_exchange_weak(head->next, head, std::memory_order_release))
                        allocator_.deallocate_unsafe(head);
                }
            }
        }

        bool pop(T& value)
        {
            auto guard = allocator_.guard();
            while (true)
            {
                auto head = allocator_.protect(head_, std::memory_order_relaxed);
                if (!head)
                    return false;

                auto top = head->stack.top_.load(std::memory_order_relaxed);
                if (head->stack.pop(value))
                    return true;

                if (!head->next)
                    return false;

                if (top.index == -1 || head->stack.top_.compare_exchange_strong(top, { T{}, (uint32_t)-1, top.counter + 1 }, std::memory_order_release))
                {
                    if (head_.compare_exchange_weak(head, head->next, std::memory_order_release))
                    {
                        std::atomic_thread_fence(std::memory_order_acquire);
                        allocator_.retire(head);
                    }
                }
            }
        }

    private:
        void clear()
        {
            auto head = head_.load(std::memory_order_acquire);
            while (head)
            {
                auto next = head->next;
                allocator_.deallocate_unsafe(head);
                head = next;
            }
        }
    };
}

//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <containers/lockfree/detail/exponential_backoff.h>
#include <containers/lockfree/atomic16.h>

#include <atomic>
#include <memory>
#include <cassert>

namespace containers
{
    //
    // Non-blocking Array-based Algorithms for Stack and Queues
    // https://link.springer.com/chapter/10.1007/978-3-540-92295-7_10
    //
    template<
        typename T,
        size_t Size,
        typename Backoff,
        uint32_t Mark = 0
    > struct bounded_stack_base
    {
        static_assert(sizeof(T) <= 8);
        static_assert(std::is_trivially_copyable_v< T >);

        struct node
        {
            alignas(8) T value;
            alignas(8) uint32_t index;
            uint32_t counter;
        };

        static_assert(sizeof(node) == 16);
        static_assert(Size > 1);

        alignas(64) atomic16< node > top_{};
        alignas(64) std::array< atomic16< node >, Size + 1 > array_{};

        using value_type = T;

        bool push(T value)
        {
            Backoff backoff;
            while (true)
            {
                auto top = top_.load(std::memory_order_relaxed);
                if (Mark && top.index == Mark)
                    return false;
                if (top.index == array_.size() - 1)
                    return false;

                // See comment below, if the stack is full, we do not need to finish the top,
                // as only operation that can be done is pop and that will finish it.
                finish(top);

                auto aboveTopCounter = array_[top.index + 1].load(std::memory_order_relaxed).counter;
                if (top_.compare_exchange_strong(top, node{ value, top.index + 1, aboveTopCounter + 1 }, std::memory_order_release))
                    return true;
                backoff();
            }
        }

        bool pop(T& value)
        {
            Backoff backoff;
            while (true)
            {
                auto top = top_.load(std::memory_order_relaxed);
                if (Mark && top.index == Mark)
                    return false;
                if (top.index == 0)
                    return false;

                // The article has finish() before if(top.index == 0), yet that worsens
                // pop() scalability in empty stack. As pop on empty stack has no effect,
                // and push() still helps with finish, it is safe.
                finish(top);

                auto belowTop = array_[top.index - 1].load(std::memory_order_relaxed);
                if (top_.compare_exchange_strong(top, node{ belowTop.value , top.index - 1, belowTop.counter + 1 }, std::memory_order_release))
                {
                    std::atomic_thread_fence(std::memory_order_acquire);
                    value = top.value;
                    return true;
                }
                backoff();
            }
        }

        static constexpr size_t capacity() { return Size; }

    private:
        void finish(node& n)
        {
            assert(!Mark || n.index != Mark);
            auto top = array_[n.index].load(std::memory_order_acquire);
            node expected = { top.value, n.index, n.counter - 1 };
            array_[n.index].compare_exchange_strong(expected, { n.value, n.index, n.counter }, std::memory_order_release);
        }
    };

    template<
        typename T,
        size_t Size,
        typename Backoff = exponential_backoff<>
    > class bounded_stack
        : private bounded_stack_base< T, Size, Backoff >
    {
    public:
        using value_type = typename bounded_stack_base< T, Size, Backoff >::value_type;

        using bounded_stack_base< T, Size, Backoff >::push;
        using bounded_stack_base< T, Size, Backoff >::pop;
        using bounded_stack_base< T, Size, Backoff >::capacity;
    };
}

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
        struct node
        {
            alignas(8) uint32_t index;
            uint32_t counter;
            alignas(8) T value;
        };

        static_assert(sizeof(node) == 16);
        static_assert(Size > 1);

        alignas(64) atomic16< node > top_{};
        alignas(64) std::array< atomic16< node >, Size + 1 > array_{};

        using value_type = T;

        template< typename... Args > bool emplace(Args&&... args)
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

                auto above_top = array_[top.index + 1].load(std::memory_order_relaxed);
                if (top_.compare_exchange_weak(top, node{ top.index + 1, above_top.counter + 1, T{ args... } }))
                    return true;
                backoff();
            }
        }

        bool push(const T& value) { return emplace(value); }

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

                auto below_top = array_[top.index - 1].load(std::memory_order_relaxed);
                if (top_.compare_exchange_weak(top, node{ top.index - 1, below_top.counter + 1, below_top.value }))
                {
                    value = std::move(top.value);
                    return true;
                }
                    
                backoff();
            }
        }

        static constexpr size_t capacity() { return Size; }

        // TODO: bool empty() const;

    private:
        void finish(node& n)
        {
            assert(!Mark || n.index != Mark);
            auto top = array_[n.index].load();
            node expected = { n.index, n.counter - 1, top.value };
            array_[n.index].compare_exchange_strong(expected, { n.index, n.counter, n.value });
        }
    };

    template<
        typename T,
        size_t Size,
        typename Backoff = exponential_backoff<>
    > class bounded_stack
        : private bounded_stack_base< T, Size, Backoff >
    {
        using base_type = bounded_stack_base< T, Size, Backoff >;
    public:
        using value_type = typename base_type::value_type;

        using base_type::emplace;
        using base_type::push;
        using base_type::pop;
        using base_type::capacity;
        // TODO: using base_type::empty;
    };
}

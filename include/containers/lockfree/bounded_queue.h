//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <containers/lockfree/detail/exponential_backoff.h>

#include <atomic>
#include <memory>
#include <optional>

namespace containers
{
    // From a BBQ article
    template<
        typename T,
        size_t Size,
        typename Backoff = exponential_backoff<>
    > class bounded_queue
    {
        static_assert(is_power_of_2<Size>::value);

        alignas(64) std::atomic< size_t > chead_{};
        alignas(64) std::atomic< size_t > ctail_{};
        alignas(64) std::atomic< size_t > phead_{};
        alignas(64) std::atomic< size_t > ptail_{};

        alignas(64) std::array< T, Size > values_;

    public:
        using value_type = T;

        template< typename... Args > bool emplace(Args&&... args)
        {
            Backoff backoff;
            while (true)
            {
                auto ph = phead_.load(std::memory_order_relaxed);
                auto pn = ph + 1;
                if (pn > ctail_.load(std::memory_order_relaxed) + Size)
                    return false;
                if (!phead_.compare_exchange_strong(ph, pn, std::memory_order_relaxed))
                {
                    backoff();
                }
                else
                {
                    values_[pn & (Size - 1)] = T{ std::forward< Args >(args)...};
                    std::atomic_thread_fence(std::memory_order_release);

                    while (ptail_.load(std::memory_order_relaxed) != ph)
                        _mm_pause();
                    ptail_.store(pn, std::memory_order_relaxed);
                    return true;
                }
            }
        }

        bool push(const T& value) { return emplace(value); }
        bool push(T&& value) { return emplace(std::move(value)); }

        bool pop(T& value)
        {
            Backoff backoff;
            while (true)
            {
                auto ch = chead_.load(std::memory_order_relaxed);
                auto cn = ch + 1;
                if (cn > ptail_.load(std::memory_order_relaxed) + 1)
                    return false;
                if (!chead_.compare_exchange_strong(ch, cn, std::memory_order_relaxed))
                {
                    backoff();
                }
                else
                {
                    std::atomic_thread_fence(std::memory_order_acquire);

                    value = std::move(values_[cn & (Size - 1)]);
                    while (ctail_.load(std::memory_order_relaxed) != ch)
                        _mm_pause();
                    ctail_.store(cn, std::memory_order_relaxed);
                    return true;
                }
            }
        }

        bool empty() const
        {
            return chead_.load(std::memory_order_relaxed) == ptail_.load(std::memory_order_relaxed);
        }

        static constexpr size_t capacity() { return Size; }
    };
}

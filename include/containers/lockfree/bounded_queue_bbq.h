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
#include <cassert>

namespace containers
{
    //
    // BBQ: A Block-based Bounded Queue
    // https://www.usenix.org/conference/atc22/presentation/wang-jiawei
    //

    constexpr size_t log2(size_t value) { return value < 2 ? 1 : 1 + log2(value / 2); }

    template<
        typename T,
        size_t Size,
        size_t BlockSize = Size / (1 << (std::max(size_t(1), log2(Size) / 4) - 1)), // log(num of blocks) = max(1, log(size)/4)
        typename Backoff = exponential_backoff<>
    > class bounded_queue_bbq
    {
        // TODO: spsc mode
        // TODO: drop mode

        enum class status
        {
            success,
            fail,
            busy,
            block_done,
        };

        struct Cursor
        {
            Cursor() = default;

            Cursor(uint32_t off, uint32_t ver)
                : version(ver)
                , offset(off)
            {}

            Cursor(uint64_t value)
                : version(value >> 32)
                , offset(value)
            {}

            operator uint64_t() { return (uint64_t)version << 32 | offset; }

            uint32_t offset;
            uint32_t version;
        };

        struct Block
        {
            alignas(64) std::atomic< uint64_t > allocated;
            alignas(64) std::atomic< uint64_t > committed;
            alignas(64) std::atomic< uint64_t > reserved;
            alignas(64) std::atomic< uint64_t > consumed;
            alignas(64) std::array< T, BlockSize > entries;
        };

        struct Entry
        {
            Block* block;
            uint32_t offset;
            uint32_t version;
        };

        static_assert(is_power_of_2< Size >::value);
        static_assert(is_power_of_2< BlockSize >::value);
        static_assert(Size / BlockSize > 1);

        alignas(64) std::array< Block, Size / BlockSize > blocks_;
        alignas(64) std::atomic< uint64_t > phead_{};
        alignas(64) std::atomic< uint64_t > chead_{};

        std::pair< Cursor, Block* > get_block(std::atomic< uint64_t >& head)
        {
            auto value = Cursor(head.load());
            return { value, &blocks_[value.offset & (blocks_.size() - 1)] };
        }

        std::pair< status, Entry > allocate_entry(Block* block)
        {
            if (Cursor(block->allocated.load()).offset >= BlockSize)
                return { status::block_done, {} };
            auto allocated = Cursor(block->allocated.fetch_add(1));
            if (allocated.offset >= BlockSize)
                return { status::block_done, {} };
            return { status::success, { block, allocated.offset, 0 } };
        }

        template< typename Ty > void commit_entry(Entry entry, Ty&& data)
        {
            entry.block->entries[entry.offset] = std::forward< Ty >(data);
            entry.block->committed.fetch_add(1);
        }

        std::pair< status, Entry > reserve_entry(Block* block, Backoff& backoff)
        {
            while (true)
            {
                auto reserved = Cursor(block->reserved.load());
                if (reserved.offset < BlockSize)
                {
                    auto committed = Cursor(block->committed.load());
                    if (committed.offset == reserved.offset)
                        return { status::fail, {} };

                    if (committed.offset != BlockSize)
                    {
                        auto allocated = Cursor(block->allocated.load());
                        if (committed.offset != allocated.offset)
                            return { status::busy, {} };
                    }

                    if (atomic_fetch_max_explicit(&block->reserved, (uint64_t)Cursor(reserved.offset + 1, reserved.version)) == (uint64_t)reserved)
                        return { status::success, { block, reserved.offset, reserved.version } };
                    else
                    {
                        backoff();
                        continue;
                    }
                }

                return { status::block_done, {} };
            }
        }

        std::optional< T > consume_entry(Entry entry)
        {
            std::optional< T > data = entry.block->entries[entry.offset];
            entry.block->consumed.fetch_add(1);
            // Drop-old mode:
            //auto allocated = entry.block->allocated.load();
            //if(allocated.version != entry.version) data.reset();
            return data;
        }

        status advance_phead(Cursor head)
        {
            auto& next_block = blocks_[(head.offset + 1) & (blocks_.size() - 1)];
            auto consumed = Cursor(next_block.consumed.load());
            if (consumed.version < head.version ||
                (consumed.version == head.version && consumed.offset != BlockSize))
            {
                auto reserved = Cursor(next_block.reserved.load());
                if (reserved.offset == consumed.offset)
                    return status::fail;
                else
                    return status::busy;
            }
            // Drop-old mode:
            //auto committed = Cursor(next_block.committed.load());
            //if (commited.version == head.version && commited.index != BlockSize)
            //    return advance_status::not_available;
            atomic_fetch_max_explicit(&next_block.committed, (uint64_t)Cursor(0, head.version + 1));
            atomic_fetch_max_explicit(&next_block.allocated, (uint64_t)Cursor(0, head.version + 1));

            // TODO: how does the article handle wrap-around?
            if (((head.offset + 1) & (blocks_.size() - 1)) == 0)
                ++head.version;

            atomic_fetch_max_explicit(&phead_, (uint64_t)Cursor(head.offset + 1, head.version));
            return status::success;
        }

        bool advance_chead(Cursor head, uint32_t version)
        {
            auto& next_block = blocks_[(head.offset + 1) & (blocks_.size() - 1)];
            auto committed = Cursor(next_block.committed.load());
            if (committed.version != head.version + 1)
                return false;
            atomic_fetch_max_explicit(&next_block.consumed, (uint64_t)Cursor(0, head.version + 1));
            atomic_fetch_max_explicit(&next_block.reserved, (uint64_t)Cursor(0, head.version + 1));
            // Drop-old mode:
            //if (committed.version < version + (head.index == 0))
            //    return false;
            //atomic_fetch_and_max(next_block.reserved, { 0, committed.version });

            // TODO: how does the article handle wrap-around?
            if (((head.offset + 1) & (blocks_.size() - 1)) == 0)
                ++head.version;

            atomic_fetch_max_explicit(&chead_, (uint64_t)Cursor(head.offset + 1, head.version));
            return true;
        }

        // Atomic minimum/maximum - https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0493r3.pdf
        template < typename U >
        U atomic_fetch_max_explicit(std::atomic<U>* pv, typename std::atomic<U>::value_type v, std::memory_order m = std::memory_order_seq_cst) noexcept
        {
            auto t = pv->load(m);
            while (std::max(v, t) != t) {
                if (pv->compare_exchange_weak(t, v, m, m))
                    break;
            }
            return t;
        }

    public:
        using value_type = T;

        bounded_queue_bbq()
        {
            blocks_[0].allocated.store(0);
            blocks_[0].committed.store(0);
            blocks_[0].reserved.store(0);
            blocks_[0].consumed.store(0);

            for (size_t i = 1; i < blocks_.size(); ++i)
            {
                auto& block = blocks_[i];
                block.allocated.store(BlockSize);
                block.committed.store(BlockSize);
                block.reserved.store(BlockSize);
                block.consumed.store(BlockSize);
            }
        }

        template< typename Ty > bool push(Ty&& value)
        {
            Backoff backoff;
            while (true)
            {
                auto [head, block] = get_block(phead_);
                auto [status, entry] = allocate_entry(block);
                switch (status)
                {
                case status::success:
                    commit_entry(entry, std::move(value));
                    return true;
                case status::block_done:
                    switch (advance_phead(head))
                    {
                    case status::success: continue;
                    case status::fail: return false;
                    case status::busy: break;
                    default: assert(false);
                    }
                default:
                    assert(false);
                }

                backoff();
            }
        }

        bool pop(T& value)
        {
            Backoff backoff;
            while (true)
            {
                auto [head, block] = get_block(chead_);
                auto [status, entry] = reserve_entry(block, backoff);
                switch (status)
                {
                case status::success: {
                    auto opt = consume_entry(entry);
                    if (opt)
                    {
                        value = std::move(*opt);
                        return true;
                    }
                    break;
                }
                case status::fail: return false;
                case status::busy: break;
                case status::block_done:
                    if (!advance_chead(head, entry.version))
                        return false;
                    continue;
                default:
                    assert(false);
                }

                backoff();
            }
        }

        static constexpr size_t capacity() { return Size; }
    };
}
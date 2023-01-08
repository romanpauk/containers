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
#include <optional>

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

    // From a BBQ article below.
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

    // TODO: A Scalable, Portable, and Memory-Efficient Lock-Free FIFO Queue - https://arxiv.org/abs/1908.04511

    // BBQ: A Block-based Bounded Queue - https://www.usenix.org/conference/atc22/presentation/wang-jiawei
    template< typename T, size_t Size, size_t BlockSize, typename Backoff = exp_backoff<> > class bounded_queue_bbq
    {
        enum class allocate_status
        {
            success,
            block_done,
        };

        enum class advance_status
        {
            success,
            no_entry,
            not_available,
        };

        enum class reserve_status
        {
            success,
            no_entry,
            not_available,
            block_done,
        };

        struct Cursor
        {
            Cursor() = default;

            Cursor(uint32_t offset, uint32_t version)
                : offset(offset)
                , version(version)
            {}

            Cursor(uint64_t value)
                : offset(value >> 32)
                , version((uint32_t)value)
            {}

            operator uint64_t() { return (uint64_t)offset << 32 | version; }

            uint32_t offset;
            uint32_t version;
        };

        struct Block
        {
            alignas(64) std::atomic< uint64_t > allocated;
            alignas(64) std::atomic< uint64_t > committed;
            alignas(64) std::atomic< uint64_t > reserved;
            alignas(64) std::atomic< uint64_t > consumed;
            alignas(64) T entries[BlockSize];
        };

        struct Entry
        {
            Block* block;
            uint32_t offset;
            uint32_t version;
        };

        static_assert(is_power_of_2< Size >::value);
        static_assert(is_power_of_2< BlockSize >::value);

        alignas(64) Block blocks_[Size / BlockSize];
        alignas(64) std::atomic< uint64_t > phead_;
        alignas(64) std::atomic< uint64_t > chead_;

        std::pair< Cursor, Block* > get_block(std::atomic< uint64_t >& head)
        {
            auto value = Cursor(head.load());
            return { value, &blocks_[value.offset] };
        }
        
        std::pair< allocate_status, Entry > allocate_entry(Block* block)
        {
            if (Cursor(block->allocated.load()).offset >= BlockSize)
                return {allocate_status::block_done, {} };
            auto offset = Cursor(block->allocated.fetch_add(1)).offset;
            if (offset >= BlockSize)
                return { allocate_status::block_done, {} };
            return { allocate_status::success, Entry { block, offset } };
        }

        template< typename Ty > void commit_entry(Entry entry, Ty&& data)
        {
            entry.block->entries[entry.offset] = std::forward< Ty >(data);
            entry.block->committed.fetch_add(1);
        }

        std::pair< reserve_status, Entry > reserve_entry(Block* block)
        {
            while (true)
            {
                auto reserved = Cursor(block->reserved.load());
                if (reserved.offset < BlockSize)
                {
                    auto committed = Cursor(block->committed.load());
                    if (committed.offset == reserved.offset)
                        return { reserve_status::no_entry, {} };

                    if (committed.offset != BlockSize)
                    {
                        auto allocated = Cursor(block->allocated.load());
                        if (committed.offset != allocated.offset)
                            return { reserve_status::not_available, {} };
                    }

                    if (atomic_fetch_and_max(block->reserved, (uint64_t)Cursor(reserved.offset + 1, reserved.version + 1)) == (uint64_t)reserved)
                        return { reserve_status::success, Entry { block, reserved.offset, reserved.version } };
                    else
                        continue;
                }

                return { reserve_status::block_done, {} };
            }
        }

        std::optional< T > consume_entry(Entry entry)
        {
            std::optional< T > data = entry.block->entries[entry.offset];
            entry.block->consumed.fetch_add(1);
            //auto allocated = entry.block->allocated.load();
            //if(allocated.version != entry.version) data.reset();
            return data;
        }

        advance_status advance_phead(Cursor head)
        {
            auto& next_block = blocks_[(head.offset + 1) & (Size / BlockSize - 1)];
            auto consumed = Cursor(next_block.consumed.load());
            if (consumed.version < head.version ||
                (consumed.version == head.version && consumed.offset != BlockSize))
            {
                auto reserved = Cursor(next_block.reserved.load());
                if (reserved.offset == consumed.offset)
                    return advance_status::no_entry;
                else
                    return advance_status::not_available;
            }
            auto committed = Cursor(next_block.committed.load());
            auto allocated = Cursor(next_block.allocated.load());
            //if (commited.version == head.version && commited.index != BlockSize)
            //    return advance_status::not_available;
            atomic_fetch_and_max(next_block.committed, (uint64_t)Cursor(committed.offset, head.version + 1));
            atomic_fetch_and_max(next_block.allocated, (uint64_t)Cursor(allocated.offset, head.version + 1));
            atomic_fetch_and_max(phead_, head + 1);
            return advance_status::success;
        }

        bool advance_chead(Cursor head, uint32_t version)
        {
            auto& next_block = blocks_[(head.offset + 1) & (Size / BlockSize - 1)];
            auto committed = Cursor(next_block.committed.load());
            if (committed.version != head.version + 1)
                return false;

            auto reserved = Cursor(next_block.reserved.load());
            atomic_fetch_and_max(next_block.committed, (uint64_t)Cursor(committed.offset, head.version + 1));
            atomic_fetch_and_max(next_block.reserved, (uint64_t)Cursor(reserved.offset, head.version + 1));
            //if (committed.version < version + (head.index == 0))
            //    return false;
            //atomic_fetch_and_max(next_block.reserved, { reserved.offset, committed.version });
            atomic_fetch_and_max(chead_, (uint64_t)Cursor(head.offset + 1, head.version + 1));
            return true;
        }

        template< typename U > U atomic_fetch_and_max(std::atomic< U >& result, U value)
        {
            auto r = result.load(std::memory_order_relaxed);
            while (r < value && !result.compare_exchange_weak(r, value))
                _mm_pause();
            return r;
        }

    public:
        template< typename Ty > bool push(Ty&& value)
        {
            while (true)
            {
                auto [head, block] = get_block(phead_);
                auto [status, entry] = allocate_entry(block);
                switch (status)
                {
                case allocate_status::success:
                    commit_entry(entry, std::move(value));
                    return true;
                case allocate_status::block_done:
                    switch (advance_phead(head))
                    {
                    case advance_status::success: continue;
                    case advance_status::no_entry: return false; // FULL
                    case advance_status::not_available: return false; // BUSY
                    default: assert(false);
                    }
                default:
                    assert(false);
                }
            }
        }

        bool pop(T& value)
        {
            while (true)
            {
                auto [head, block] = get_block(chead_);
                auto [status, entry] = reserve_entry(block);
                switch (status)
                {
                case reserve_status::success: {
                    auto opt = consume_entry(entry);
                    if (opt)
                    {
                        value = std::move(*opt);
                        return true;
                    }

                    continue;
                }
                case reserve_status::no_entry:
                    return false; // EMPTY
                case reserve_status::not_available:
                    return false; // BUSY
                case reserve_status::block_done:
                    if(advance_chead(head, entry.version))
                        continue;
                    else
                        return false; // EMPTY
                default:
                    assert(false);
                }
            }
        }
    };
}

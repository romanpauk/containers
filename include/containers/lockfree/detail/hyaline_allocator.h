//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

//
// Snapshot-Free, Transparent, and Robust Memory Reclamation for Lock-Free Data Structures
// https://arxiv.org/pdf/1905.07903
//

#include <containers/lockfree/detail/aligned.h>
#include <containers/lockfree/detail/thread_manager.h>

#include <cassert>
#include <array>
#include <vector>
#include <atomic>
#include <algorithm>

// TODO: not finished, just for perf testing

namespace containers::detail
{
    template< size_t N > struct hyaline1
    {
        struct node_t
        {
            union
            {
                std::atomic< int > ref;
                node_t* next;
            };

            node_t* ref_node;
            node_t* batch_node;
        };

        struct head_t
        {
            //int ref; // for Hyaline-1 version, the ref is always 1 and can be part of ptr
            node_t* ptr;
        };

        struct local_batch_t
        {
            node_t* ref_node;
            node_t* node;
            // int minEra // For Hyaline-s or -1s
        };

        struct guard
        {
            guard(size_t id)
                : id_(id & (heads_.size() - 1))
                , handle_(enter(id_))
            {}

            ~guard() { leave(id_, handle_); }

        private:
            size_t id_;
            node_t* handle_;
        };

        static node_t* enter(size_t id)
        {
            heads_[id].store({nullptr}); // TODO: ref part of ptr
            return nullptr;
        }

        static void leave(size_t id, node_t* node)
        {
            auto head = heads_[id].exchange({ nullptr });
            if (head.ptr != nullptr)
                traverse(head.ptr, node);
        }

        static void traverse(node_t* next, node_t* node)
        {
            node_t* current = nullptr;
            do
            {
                current = next;
                if (!current)
                    break;
                auto ref = current->ref_node;
                if (ref->ref.fetch_add(-1) == 1)
                    ;//free_batch(ref->batch_next);
            } while (current != node);
        }

        static void retire(local_batch_t* batch)
        {
            // int empty = 0;
            int inserts = 0;
            auto current = batch->node;
            batch->ref_node->ref = 0;

            for (size_t i = 0; i < heads_.size(); ++i)
            {
                head_t head{};
                head_t new_head{};
                do
                {
                    head = heads_[i];
                    if (head.ptr == nullptr)
                    {
                        // empty += 1; // TODO: Adjs?

                        // TODO: continue with next I
                    }
                    new_head.ptr = current;
                    // new_head.ref = 1...
                    new_head.ptr->next = head.ptr;
                }
                while(false); //!heads_[i].compare_exchange(head, new_head));

                current = current->batch_node;
                inserts++; // #2# //adjust(head->ptr, Adjs + 1 /* head.ref */);
            }

            adjust(batch->node, inserts); // #3#
        }

        static void adjust(node_t* node, int value)
        {
            auto ref_node = node->ref_node;
            if (ref_node->ref.fetch_add(value) == -value); // TODO: free_batch
        }

        alignas(64) static std::array< detail::aligned< std::atomic< head_t > >, N > heads_;
    };

    template< size_t N >
    std::array< detail::aligned< std::atomic< typename hyaline1< N >::head_t > >, N > hyaline1< N >::heads_;

    template<
        typename T,
        typename Allocator = std::allocator< T >,
        typename ThreadManager = thread,
        typename Hyaline = hyaline1< thread::max_threads >
    > class hyaline_allocator
        : public Hyaline
    {
        // template< typename U, typename AllocatorU > friend class hyaline_allocator;
        
        struct buffer
        {
            template< typename... Args > buffer(Args&&... args)
                : value{ std::forward< Args >(args)... }
            {}

            typename Hyaline::node_t node{};
            T value;
        };

        using allocator_type = typename std::allocator_traits< Allocator >::template rebind_alloc< buffer >;
        using allocator_traits_type = std::allocator_traits< allocator_type >;

        allocator_type allocator_;

        static buffer* buffer_cast(T* ptr)
        {
            return reinterpret_cast<buffer*>(reinterpret_cast<uintptr_t>(ptr) - offsetof(buffer, value));
        }
        
    public:
        template< typename U > struct rebind
        {
            using other = hyaline_allocator< U, typename std::allocator_traits< Allocator >::template rebind_alloc< U > >;
        };

        hyaline_allocator() {};
        template< typename U, typename AllocatorT > hyaline_allocator(hyaline_allocator< U, AllocatorT >&) {}

        // TODO: using token() no longer guarantees each thread gets its own slot automatically.
        // But using thread-local id() was too slow. Calling instance is slow.
        auto guard() { return Hyaline::guard(ThreadManager::token()); }

        template< typename... Args > T* allocate(Args&&... args)
        {
            auto ptr = allocator_traits_type::allocate(allocator_, 1);
            allocator_traits_type::construct(allocator_, ptr, std::forward< Args >(args)...);
            return &ptr->value;
        }

        T* protect(const std::atomic< T* >& value, std::memory_order order = std::memory_order_seq_cst)
        {
            return value.load(order);
        }
 
        void retire(T* ptr)
        {
            Hyaline::local_batch_t batch;
            batch.node = &buffer_cast(ptr)->node;
            // Hyaline::retire(&batch);
        }

        void deallocate(T* ptr)
        {
            auto buffer = buffer_cast(ptr);
            allocator_traits_type::destroy(allocator_, buffer);
            allocator_traits_type::deallocate(allocator_, buffer, 1);
        }
    };
}

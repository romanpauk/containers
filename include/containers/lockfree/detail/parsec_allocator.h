//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

// Scalable Memory Reclamation for Multi- Core, Real-Time Systems

// Hazard Eras - Non-Blocking Memory Reclamation - https://github.com/pramalhe/ConcurrencyFreaks/blob/master/papers/hazarderas-2017.pdf
// Universal Wait-Free Memory Reclamation - https://arxiv.org/abs/2001.01999

#include <containers/lockfree/detail/aligned.h>
#include <containers/lockfree/detail/thread_manager.h>

#include <cassert>
#include <array>
#include <vector>
#include <atomic>
#include <algorithm>

namespace containers::detail
{
    template< typename ThreadManager = thread > class parsec_allocator_base
    {
    public:
        struct hazard_buffer_header
        {
            uint64_t retired;
        };

        // deleter is a static function with proper allocator type, so from any hazard_era_allocator<*>
        // 'correct' deallocation code is invoked on correctly aligned type to deallocate.
        // deleter usage for data in shared memory is problematic:
        //  1) obviously this would work only in same languages sharing the same destruction code. deleter
        //      would have to be replaced by type and the function address resolved for each process.
        //  2) node-based stacks and queues use destructor to invoke optional<T> destructor, yet if T is
        //      trivially destructible, there is nothing to invoke.

        using deleter = void (*)(hazard_buffer_header*);

        struct thread_data
        {
            uint64_t enter;
            uint64_t exit;
            uint64_t retired;

            // TODO: retire list needs to be passed to someone upon thread exit
            std::vector< std::pair< hazard_buffer_header*, deleter > > retired_buffers;

            void clear()
            {
                enter = exit = 0;
            }
        };
        
        alignas(64) std::array< detail::aligned< thread_data >, ThreadManager::max_threads > thread;

        struct thread_guard
        {
            thread_guard() { instance().enter(ThreadManager::instance().id()); }
            ~thread_guard() { instance().exit(ThreadManager::instance().id()); }
        };

        static parsec_allocator_base< ThreadManager >& instance()
        {
            static parsec_allocator_base< ThreadManager > instance;
            return instance;
        }

        thread_guard guard() { return thread_guard(); }

        size_t thread_id() { return ThreadManager::instance().id(); }

        uint64_t quiesce()
        {
            uint64_t q = timestamp();
            for (auto& t : thread)
            {
                if(t.exit < t.enter) q = std::min(t.enter, q);
            }

            return q;
        }

        void cleanup()
        {
            auto q = quiesce();
            auto& buffers = thread[thread_id()].retired_buffers;
            buffers.erase(std::remove_if(buffers.begin(), buffers.end(), [this, q](const std::pair< hazard_buffer_header*, deleter >& p)
            {
                if (p.first->retired < q)
                {
                    p.second(p.first);
                    return true;
                }

            return false;
            }), buffers.end());
        }

        void enter(size_t tid)
        {
            thread[tid].enter = timestamp();
        }

        void exit(size_t tid)
        {
            thread[tid].exit = thread[tid].enter + 1;
        }

        uint64_t timestamp()
        {
            return GetTickCount64();

            static std::atomic< uint64_t > epoch;
            static thread_local uint64_t counter;
            if((counter++ & 1023) == 0)
                return epoch.fetch_add(1, std::memory_order_relaxed);
            return epoch.load(std::memory_order_relaxed);
        }
    };

    template< typename T, typename ThreadManager = thread, typename Allocator = std::allocator< T > > class parsec_allocator
    {
        template< typename U, typename ThreadManagerU, typename AllocatorU > friend class parsec_allocator;

        static_assert(std::is_empty_v< Allocator >);

        static const int freq = 1024;
        static_assert(freq % 2 == 0);

        using hazard_buffer_header = typename parsec_allocator_base< ThreadManager >::hazard_buffer_header;

        struct hazard_buffer
        {
            template< typename... Args > hazard_buffer(Args&&... args)
                : header{}
                , value{ std::forward< Args >(args)... }
            {}

            hazard_buffer_header header;
            T value;
        };

        using allocator_type = typename std::allocator_traits< Allocator >::template rebind_alloc< hazard_buffer >;
        using allocator_traits_type = std::allocator_traits< allocator_type >;

    public:
        template< typename U > struct rebind
        {
            using other = parsec_allocator< U, ThreadManager, typename std::allocator_traits< Allocator >::template rebind_alloc< U > >;
        };
        
        parsec_allocator() {}
        template< typename U, typename AllocatorT > parsec_allocator(parsec_allocator< U, ThreadManager, AllocatorT >&) {}

        auto guard() { return base().guard(); }
        auto thread_id() { return base().thread_id(); }

        template< typename... Args > T* allocate(Args&&... args)
        {
            allocator_type allocator;
            auto buffer = allocator_traits_type::allocate(allocator, 1);
            allocator_traits_type::construct(allocator, buffer, std::forward< Args >(args)...);

            return &buffer->value;
        }

        T* protect(std::atomic< T* >& value, std::memory_order order = std::memory_order_seq_cst)
        {
            return value.load(order);
        }

        void retire(T* ptr)
        {
            auto tid = thread_id();
            auto buffer = hazard_buffer_cast(ptr);
            buffer->header.retired = base().thread[tid].enter; //base().timestamp();
            base().thread[tid].retired_buffers.emplace_back(&buffer->header, &hazard_buffer_retire);

            if ((base().thread[tid].retired++ & (freq - 1)) == 0)
            {
                base().cleanup();
            }
        }

        void deallocate(T* ptr)
        {
            hazard_buffer_deallocate(hazard_buffer_cast(ptr));
        }

    private:
        parsec_allocator_base< ThreadManager >& base()
        {
            return parsec_allocator_base< ThreadManager >::instance();
        }
        
        static void hazard_buffer_retire(hazard_buffer_header* ptr)
        {
            hazard_buffer_deallocate(hazard_buffer_cast(ptr));
        }

        static void hazard_buffer_deallocate(hazard_buffer* buffer)
        {
            allocator_type allocator;
            allocator_traits_type::destroy(allocator, buffer);
            allocator_traits_type::deallocate(allocator, buffer, 1);
        }

        static hazard_buffer* hazard_buffer_cast(T* ptr)
        {
            return reinterpret_cast< hazard_buffer* >(reinterpret_cast<uintptr_t>(ptr) - offsetof(hazard_buffer, value));
        }

        static hazard_buffer* hazard_buffer_cast(hazard_buffer_header* ptr)
        {
            return reinterpret_cast< hazard_buffer* >(reinterpret_cast<uintptr_t>(ptr) - offsetof(hazard_buffer, header));
        }
    };
}

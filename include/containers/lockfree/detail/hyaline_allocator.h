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

#define DEBUG_(...)
//#define DEBUG_(...) fprintf(stderr, __VA_ARGS__)

namespace containers::detail
{
    // TODO: this should just be freelist of buffers of some size, so they can be reused between different allocator instances
    template< typename T, typename Backoff = detail::exponential_backoff<>, typename Allocator = std::allocator< T > > class free_list {
        struct buffer {
            buffer(): value() {}
            union {
                T value;
                buffer* next;
            };
        };

        alignas(64) aligned< std::atomic< buffer* > > head_{};
        
        using allocator_type = typename std::allocator_traits< Allocator >::template rebind_alloc< buffer >;
        using allocator_traits_type = std::allocator_traits< allocator_type >;
        alignas(64) allocator_type allocator_; // TODO: move out

    public:
        ~free_list() {
            clear();
        }
        
        T* allocate() {
            Backoff backoff;
            while (true) {
                auto head = head_.load();
                if (!head) {
                    auto ptr = allocator_traits_type::allocate(allocator_, 1);
                    allocator_traits_type::construct(allocator_, ptr);
                    return &ptr->value;
                }

                if (head_.compare_exchange_weak(head, head->next)) {
                    return &head->value;
                }

                backoff();
            }
        }

        void deallocate(T* ptr) {
            auto head = buffer_cast(ptr);
            Backoff backoff;
            while (true) {
                head->next = head_.load();
                if(head_.compare_exchange_weak(head->next, head))
                    break;
                backoff();
            }
        }

    private:
        static buffer* buffer_cast(T* ptr) {
            return reinterpret_cast<buffer*>(reinterpret_cast<uintptr_t>(ptr) - offsetof(buffer, value));
        }

        void clear() {
            buffer* head = head_.load();
            while (head) {
                buffer* next = head->next;
                allocator_traits_type::deallocate(allocator_, head, 1);
                head = next;
            }
        }
    };

    template<
        typename T,
        typename Allocator = std::allocator< T >,
        typename ThreadManager = thread,
        size_t N = ThreadManager::max_threads
    > class hyaline_allocator {
        using hyaline_allocator_type = hyaline_allocator< T, Allocator, ThreadManager >;

        static constexpr size_t Adjs = uint64_t(-1) / N + 1; // Needed for different Hyaline version

        struct node_t {
            std::atomic< int64_t > ref;
            // TODO: this should be a buffer of allocated objects
        };

        struct node_list_t {
            node_list_t() = default;
            std::atomic< node_list_t* > next;
            size_t id;
            node_t* node;
        };

        alignas(64) std::array< aligned< free_list< node_list_t > >, N > node_lists_;
        
        struct head_t {
            head_t() = default;
            head_t(node_list_t* node, uint64_t ref) {
                address = reinterpret_cast<uintptr_t>(node) | ref;
            }

            node_list_t* get_ptr() { return reinterpret_cast<node_list_t*>(address & ~1); }
            uint64_t get_ref() { return address & 1; }

        private:
            union {
                node_list_t* ptr;
                uintptr_t address;
            };
        };

        alignas(64) std::array< detail::aligned< std::atomic< head_t > >, N > heads_;

        struct guard_class {
            guard_class(hyaline_allocator_type& allocator, size_t id)
                : allocator_(allocator)
                , id_(id & (allocator.heads_.size() - 1))
                , end_(allocator.enter(id_)) {}

            ~guard_class() { allocator_.leave(id_, end_); }

        private:
            hyaline_allocator_type& allocator_;
            size_t id_;
            node_list_t* end_;
        };

        node_list_t* enter(size_t id) {
            heads_[id].store({ nullptr, 1 });
            return nullptr;
        }

        void leave(size_t id, node_list_t* node) {
            auto head = heads_[id].exchange({ nullptr, 0 });
            if (head.get_ptr() != nullptr)
                traverse(head.get_ptr(), node);
        }

        void traverse(node_list_t* node, node_list_t* end) {
            DEBUG_("[%llu] traverse(): %p\n", thread::id(), node);
            node_list_t* current = nullptr;
            do {
                current = node;
                if (!current)
                    break;
                node = current->next;
                if (current->node->ref.fetch_add(-1) == 1) {
                    free(current);
                }
            } while (current != end);
        }

        void retire(node_t* node) {
            int inserts = 0;
            node->ref = 0;
            
            auto id = thread::id();
            for (size_t i = 0; i < heads_.size(); ++i) {
                head_t head{};
                head_t new_head{};
                do {
                    head = heads_[i];
                    if (head.get_ref() == 0)
                        goto next;

                    auto n = node_lists_[id].allocate();
                    n->id = id;
                    n->next = nullptr;
                    n->node = node;
        
                    new_head = head_t(n, head.get_ref());
                    n->next = head.get_ptr();
                } while (!heads_[i].compare_exchange_strong(head, new_head));
                ++inserts;
            next:
                ;
            }

            // TODO: How is this supposed to work if it frees the node but not removes it from the list?
            adjust(node, inserts);
        }

        void adjust(node_t* node, int value) {
            if (node->ref.fetch_add(value) == -value) {
                std::abort(); // TODO
                //free(node);
            }
        }

        void free(node_list_t* node) {
            deallocate(buffer_cast(node->node));
            node_lists_[node->id].deallocate(node);
        }
        
        struct buffer {
            template< typename... Args > buffer(Args&&... args)
                : value{ std::forward< Args >(args)... }
            {}

            node_t node{};
            T value;
        };

        using allocator_type = typename std::allocator_traits< Allocator >::template rebind_alloc< buffer >;
        using allocator_traits_type = std::allocator_traits< allocator_type >;

        allocator_type allocator_;

        static buffer* buffer_cast(T* ptr) {
            return reinterpret_cast<buffer*>(reinterpret_cast<uintptr_t>(ptr) - offsetof(buffer, value));
        }

        static buffer* buffer_cast(node_t* ptr) {
            return reinterpret_cast<buffer*>(reinterpret_cast<uintptr_t>(ptr) - offsetof(buffer, node));
        }

        void deallocate(buffer* ptr) {
            allocator_traits_type::destroy(allocator_, ptr);
            allocator_traits_type::deallocate(allocator_, ptr, 1);
        }

    public:
        template< typename U > struct rebind {
            using other = hyaline_allocator< U, typename std::allocator_traits< Allocator >::template rebind_alloc< U > >;
        };

        hyaline_allocator() {};
        template< typename U, typename AllocatorT > hyaline_allocator(hyaline_allocator< U, AllocatorT >&) {}

        // TODO: in some cases using token() is a bit faster (sometimes around 10%). On the other hand non-sequential
        // id requires DCAS later. For simplicity, try to finish this with id() and see.
        auto guard() { return guard_class(*this, ThreadManager::id()); }

        template< typename... Args > T* allocate(Args&&... args) {
            auto ptr = allocator_traits_type::allocate(allocator_, 1);
            allocator_traits_type::construct(allocator_, ptr, std::forward< Args >(args)...);
            return &ptr->value;
        }

        T* protect(const std::atomic< T* >& value, std::memory_order order = std::memory_order_seq_cst) {
            return value.load(order);
        }

        // TODO: store the retired node on guard
        void retire(T* ptr) {
            retire(&buffer_cast(ptr)->node);
        }

        void deallocate(T* ptr) {
            deallocate(buffer_cast(ptr));
        }
    };
}

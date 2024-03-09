//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <cassert>
#include <memory>
#include <thread>

namespace containers::detail {

    template< typename T > struct wide_ptr {
        T* ptr;
        uint64_t counter;
    };

    template< typename T > class stack {
        static_assert(sizeof(wide_ptr<T>) == 16);
        alignas(16) std::atomic< wide_ptr<T> > head_{};

    public:
        ~stack() {
            assert(!head_.load().ptr);
        }
        
        void push(T* head) {
            auto old_head = head_.load(std::memory_order_relaxed)
            while (old_head.ptr) {
                head->next = old_head.ptr;
                if(head_.compare_exchange_weak(old_head, {head, old_head.counter + 1}, std::memory_order_release))
                   break;
            }
        }

        T* pop() {
            auto old_head = head_.load(std::memory_order_relaxed);
            while (old_head.ptr) {
                if (head_.compare_exchange_weak(old_head, {old_head.ptr->next, old_head.counter + 1}, std::memory_order_release))
                    return old_head.ptr;
            }
            return nullptr;
        }
    };

    template< typename T, typename Allocator = std::allocator<T> > struct thread_data: Allocator {
        static T* get() {
            struct thread {
                thread() {
                    if (auto p = stack_.pop()) {
                        ptr = p;
                    } else {
                        ptr = std::allocator_traits<Allocator>::allocate(*this, 1);
                        std::allocator_traits<Allocator>::construct(ptr);
                    }
                }
                ~thread() {
                    stack_.push(ptr);
                }

                T* ptr;
            };

            static thread_local thread data;
            return data.ptr;
        }

        // TODO: delete stack_ elements
        static stack<T> stack_;
    };

    template< typename T, typename Allocator > stack<T> thread_data<T, Allocator>::stack_;

    template< typename Thread, typename Buffer > struct thread_hash_table {
        void enter(Thread* thread) { 
            size_t id = thread->hash & (threads_.size() - 1);
            auto old_head = threads_[id].load(std::memory_order_relaxed)
            while (old_head.ptr) {
                head->next = old_head.ptr;
                if(threads_[id].compare_exchange_weak(old_head, {thread, old_head.counter + 1}, std::memory_order_release))
                   break;
            }
        }

        void leave(Thread* thread) {
            size_t id = thread->hash & (threads_.size() - 1);
            
        }

        void reclaim(Buffer*) {
            // TODO: protect buffer refcount by inflating it so it can't be deleted until added everywhere
            
            for (size_t i = 0; i < threads_.size(); ++i) {
                //threads_[i].for_each([](Thread* thread){
                    //thread->buffers.
                //});
            }

            // TODO: unprotect refcount
        }

        std::array< wide_ptr<T>, 32 > threads_{};
    }; 
    
    template< 
        typename T, 
        typename Allocator, 
        typename AllocatorBase = typename std::allocator_traits<Allocator>::template rebind_alloc<uint8_t> 
    > class reclaiming_allocator: public AllocatorBase {
        struct buffer {
            size_t size = 0;
            std::atomic<size_t> refs = 0;
            void (*dtor)(void*);
        };

        struct thread_guard {
            thread_guard(reclaiming_allocator<T, Allocator, AllocatorBase>& allocator): allocator_(allocator) {}
            ~thread_guard() { allocator_.leave(); }

            reclaiming_allocator<T, Allocator, AllocatorBase>& allocator_;
        };

        buffer* buffer_cast(T* ptr) {
            return reinterpret_cast<buffer*>(reinterpret_cast<uintptr_t>(ptr) - sizeof(buffer));
        }

        alignas(16) struct thread {
            thread() {
                // hash = hash(this);
            }

            std::atomic<wide_ptr<Thread>> next;
            
            size_t hash;
            // delete buffer list
        };

        void leave() {
            threads_.leave(thread_data<thread>::get());
            // process delete buffers
        }
        
        threads_hash_table< thread*, buffer* > threads_;

    public:
        using value_type = T;

        ~reclaiming_allocator() {
            reset();
        }

        thread_guard enter() { 
            threads_.enter(thread_data<thread>::get());
            return *this; 
        }
        
        T* allocate(size_t n) {
            static_assert(sizeof(buffer) == 16);
            buffer* ptr = (buffer*)AllocatorBase::allocate(sizeof(buffer) + sizeof(T) * n);
            ptr->size = sizeof(buffer) + sizeof(T) * n;
            return reinterpret_cast<T*>(ptr + 1);
        }
        
        void reclaim(T* ptr, size_t) {
            threads_.reclaim(buffer_cast(ptr));
        }

        void reset() {
            //
        }
    };
}

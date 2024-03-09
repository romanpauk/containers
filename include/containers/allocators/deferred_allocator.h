//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <cassert>
#include <memory>

namespace containers::detail {
    template< 
        typename T, 
        typename Allocator, 
        typename AllocatorBase = typename std::allocator_traits<Allocator>::template rebind_alloc<uint8_t> 
    > class deferred_allocator: public AllocatorBase {
        struct buffer {
            buffer* next = nullptr;
            size_t size = 0;
        };

        struct thread_guard {};

#if 1
        template< typename U > class stack {
            template< typename V > struct wide_ptr {
                V* ptr;
                uint64_t counter;
            };

            static_assert(sizeof(wide_ptr<U>) == 16);
            alignas(16) std::atomic< wide_ptr<U> > head_{};
        public:
            void push(U* head) {
                auto old_head = head_.load(std::memory_order_relaxed);
                while (true) {
                    head->next = old_head.ptr;
                    if(head_.compare_exchange_weak(old_head, {head, old_head.counter + 1}))
                        return;
                }
            }

            U* pop() {
                auto old_head = head_.load(std::memory_order_relaxed);
                while (old_head.ptr) {
                    if (head_.compare_exchange_weak(old_head, {old_head.ptr->next, old_head.counter + 1}))
                        return old_head.ptr;
                }
                return nullptr;
            }
        };
#else
        template< typename U > struct stack {
            void push(U* value) {
                assert(value);
                value->next = head_.next;
                head_.next = value;
            }

            U* top() {
                return head_.next; 
            }

            U* pop() {
                U* value = head_.next;
                head_.next = value ? value->next : nullptr;
                return value;
            }

        private:
            U head_;
        };
#endif

        buffer* buffer_cast(T* ptr) {
            return reinterpret_cast<buffer*>(reinterpret_cast<uintptr_t>(ptr) - sizeof(buffer));
        }

        stack<buffer> stack_;

    public:
        using value_type = T;

        ~deferred_allocator() {
            reset();
        }

        thread_guard enter() { return {}; }

        T* allocate(size_t n) {
            static_assert(sizeof(buffer) == 16);
            buffer* ptr = (buffer*)AllocatorBase::allocate(sizeof(buffer) + sizeof(T) * n);
            ptr->next = nullptr;
            ptr->size = sizeof(buffer) + sizeof(T) * n;
            return reinterpret_cast<T*>(ptr + 1);
        }
        
        void reclaim(T* ptr, size_t) {
            stack_.push(buffer_cast(ptr));
        }

        void reset() {
            while(auto ptr = stack_.pop())
                AllocatorBase::deallocate((uint8_t*)ptr, ptr->size);
        }
    };
}

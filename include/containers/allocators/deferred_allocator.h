//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <cassert>
#include <limits>
#include <memory>

namespace containers::detail {
    template<
        typename T,
        typename Allocator,
        typename AllocatorBase = typename std::allocator_traits<Allocator>::template rebind_alloc<uint8_t>
    > class deferred_allocator: public AllocatorBase {
        struct buffer {
            buffer* next = nullptr;
            std::size_t size = 0;
        };

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

        buffer* buffer_cast(T* ptr) {
            assert(ptr);
            return reinterpret_cast<buffer*>(reinterpret_cast<uintptr_t>(ptr) - sizeof(buffer));
        }

        stack<buffer> stack_;

    public:
        using value_type = T;

        ~deferred_allocator() {
            reset();
        }

        value_type* allocate(std::size_t n) {
            if (std::numeric_limits<std::size_t>::max() / sizeof(T) < n)
                return nullptr;
            if (std::numeric_limits<std::size_t>::max() - sizeof(buffer) < sizeof(T) * n)
                return nullptr;
            std::size_t bytes = sizeof(buffer) + sizeof(T) * n;
            buffer* ptr = (buffer*)AllocatorBase::allocate(bytes);
            ptr->next = nullptr;
            ptr->size = bytes;
            return reinterpret_cast<T*>(ptr + 1);
        }

        void deallocate(value_type* ptr, std::size_t) {
            stack_.push(buffer_cast(ptr));
        }

        void reset() {
            while(auto ptr = stack_.pop())
                AllocatorBase::deallocate((uint8_t*)ptr, ptr->size);
        }
    };
}

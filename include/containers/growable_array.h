//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <memory>
#include <mutex>
#include <thread>
#include <cstdlib>
#include <cstring>

namespace containers {
    // Single writer, multiple readers dynamic append-only array.
    template< typename T, typename Allocator = std::allocator<T>, size_t BlockSize = 1 << 10, size_t BlocksGrowFactor = 2 >
    class growable_array: Allocator {
        struct block {
            static constexpr size_t capacity() {
                static_assert((BlockSize & (BlockSize - 1)) == 0);
                return BlockSize;
            }

            template< typename... Args > void emplace(T* ptr, Args&&... args) {
                new (ptr) T{std::forward<Args>(args)...};
            }

            T& operator[](size_t n) {
                return *at(n);
            }

            T* begin() { return at(0); }

            void destroy(size_t size) {
                if (size > 0) {
                    do {
                        this->at(--size)->~T();
                    } while (size);
                }
            }

        protected:
            T* at(size_t n) {
                T* ptr = reinterpret_cast<T*>(storage_.data()) + n;
                assert((reinterpret_cast<uintptr_t>(ptr) & (alignof(T) - 1)) == 0);
                return ptr;
            }

            std::array<uint8_t, sizeof(T) * capacity()> storage_;
        };

        struct block_map {
            block_map* next = 0;
            block* blocks[0];
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

        std::atomic<size_t> size_ = 0;
        size_t map_size_ = 0;
        size_t map_capacity_ = 0;
        stack<block_map> maps_;

        //template< typename U > U* allocate(size_t n) {
            //typename std::allocator_traits<Allocator>::template rebind_alloc<U> allocator(*this);
            //U* ptr = allocator.allocate(n);
            //allocator.construct(ptr);
            //return ptr;
        //}

        //template< typename U > void deallocate(U* ptr, size_t n) {
        //    typename std::allocator_traits<Allocator>::template rebind_alloc<U> allocator(*this);
        //    allocator.deallocate(ptr, n);
        //}

        static constexpr size_t log2(size_t n) { return ((n<2) ? 1 : 1 + log2(n/2)); }

        T& read(size_t size, size_t n) {
            assert(n < size);
            assert(map_);
            auto index = n >> (log2(block::capacity()) - 1);
            auto offset = n & (block::capacity() - 1);
            return (*maps_.top()->blocks[index])[offset];
        }
    public:
        using value_type = T;
        
        class reader_state {
            template< typename U, typename AllocatorU, size_t, size_t > friend class growable_array;
            size_t size;
        };

        ~growable_array() {
            clear();
        }

        void clear() {
            if (map_size_ > 0) {
                auto map = maps_.pop();
                assert(map);
                auto size = size_.exchange(0, std::memory_order_relaxed);
                do {
                    --map_size_;
                    if (!std::is_trivially_destructible_v<T>) {
                        auto count = size & (block::capacity() - 1);
                        map->blocks[map_size_]->destroy(count);
                        size -= count;
                    }
                    //deallocate<block>(map_[map_size_], 1);
                    delete map->blocks[map_size_];
                } while (map_size_);
                
                // deallocate<block*>(map_, map_capacity_);
                std::free(map);
                map_capacity_ = 0;
                map_size_ = 0;
                
                while ((map = maps_.pop())) {
                    std::free(map);
                }
            }
        }

        const T& operator[](size_t n) const {
            return const_cast<growable_array<T>&>(*this)->operator[](n);
        }

        T& operator[](size_t n) {
            return read(size_.load(std::memory_order_acquire), n);
        }

        T& read(reader_state& state, size_t n) {
            if (n >= state.size)
                state.size = size_.load(std::memory_order_acquire);
            return read(state.size, n);
        }

        size_t size() const { return size_.load(std::memory_order_acquire); }
        size_t empty() const { return size_.load(std::memory_order_acquire) == 0; }

        template< typename... Args > size_t emplace_back(Args&&... args) {
            size_t size = size_.load(std::memory_order_relaxed); 
            size_t index = size >> (log2(block::capacity()) - 1);
            size_t offset = size & (block::capacity() - 1);

            if (maps_.top()) {
                assert(map_size_ > 0);
                if (index < map_size_) {
                insert:
                    maps_.top()->blocks[index]->emplace(
                        maps_.top()->blocks[index]->begin() + offset, std::forward<Args>(args)...);
                    size_.store(size + 1, std::memory_order_release);
                    return size + 1;
                } else if (map_size_ < map_capacity_) {
                    maps_.top()->blocks[map_size_++] = new block(); // allocate<block>(1);
                    goto insert;
                } else {
                    auto map = (block_map*)std::malloc(sizeof(block_map) + sizeof(block*) * map_capacity_ * BlocksGrowFactor);
                    std::memcpy(map->blocks, maps_.top()->blocks, sizeof(block*) * map_capacity_);
                    map->blocks[map_size_++] = new block(); // allocate<block>(1);
                    map_capacity_ *= BlocksGrowFactor;
                    maps_.push(map);
                    goto insert;
                }
            } else {
                auto map = (block_map*)std::malloc(sizeof(block_map) + sizeof(block*) * BlocksGrowFactor);
                map->blocks[0] = new block(); //allocate<block>(1);
                map_size_ = 1;
                map_capacity_ = BlocksGrowFactor;
                maps_.push(map);
                goto insert;
            }
        }

        size_t push_back(const T& value) { return emplace_back(value); }
        size_t push_back(T&& value) { return emplace_back(std::move(value)); }
    };
}

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

#if defined(__linux__)
#include <sys/mman.h>
#endif

namespace containers {

#if defined(__linux__)
    template< typename T, size_t Capacity = 1 << 30 > class mmapped_array {
        static constexpr size_t capacity_ = Capacity;
        size_t size_ = 0;
        void* data_ = nullptr;

    public:
        ~mmapped_array() { munmap(data_, capacity_); }

        template< typename Ty > void push_back(Ty&& value) {
            if (!data_) {
                data_ = mmap(0, capacity_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if ((uintptr_t)data_ == -1)
                    std::abort();
            }

            new(reinterpret_cast<T*>(data_) + size_++) T(std::forward<Ty>(value));
        }
    };
#endif

    // Single writer, multiple readers dynamic append-only array.
    template< typename T, typename Allocator = std::allocator<T>, size_t BlockSize = 1 << 8, size_t BlocksGrowFactor = 2 >
    class growable_array: Allocator {
        struct block_trivially_destructible {
            //static_assert(std::is_trivially_destructible_v<T>);

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

        protected:
            T* at(size_t n) {
                T* ptr = reinterpret_cast<T*>(storage_.data()) + n;
                assert((reinterpret_cast<uintptr_t>(ptr) & (alignof(T) - 1)) == 0);
                return ptr;
            }

            std::array<uint8_t, sizeof(T) * capacity()> storage_;
        };

        struct block_destructible: block_trivially_destructible {
            //static_assert(std::is_trivially_destructible_v<T>);

            ~block_destructible() {
                //if (this->size_ > 0) {
                //    do {
                //        this->at(--this->size_)->~T();
                //    } while (this->size_);
                //}
            }
        };

        using block = std::conditional_t< std::is_trivially_destructible_v<T>, block_trivially_destructible, block_destructible >;

        struct block_map {
            block_map* next = 0;
            block* blocks[0];
        };

        template< typename U > struct stack {
            void push(U* value) {
                value->next = head_.next;
                head_.next = value;
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
        block_map* map_ = nullptr;
        size_t map_size_ = 0;
        size_t map_capacity_ = 0;
        stack<block_map> retired_maps_;

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
            return (*map_->blocks[index])[offset];
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
                do {
                    --map_size_;
                    if (!std::is_trivially_destructible_v<block>)
                        map_->blocks[map_size_]->~block();
                    //deallocate<block>(map_[map_size_], 1);
                    delete map_->blocks[map_size_];
                } while (map_size_);
                
                // deallocate<block*>(map_, map_capacity_);
                std::free(map_);
                map_ = nullptr;
                map_capacity_ = 0;
                map_size_ = 0;
                size_.store(0, std::memory_order_relaxed);

                while (auto* retired = retired_maps_.pop()) {
                    std::free(retired);
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

            if (map_) {
                assert(map_size_ > 0);
                if (index < map_size_) {
                insert:
                    map_->blocks[index]->emplace(
                        map_->blocks[index]->begin() + offset, std::forward<Args>(args)...);
                    size_.store(size + 1, std::memory_order_release);
                    return size + 1;
                } else if (map_size_ < map_capacity_) {
                    map_->blocks[map_size_++] = new block(); // allocate<block>(1);
                    goto insert;
                } else {
                    auto map = (block_map*)std::malloc(sizeof(block_map) + sizeof(block*) * map_capacity_ * BlocksGrowFactor);
                    std::memcpy(map->blocks, map_->blocks, sizeof(block*) * map_capacity_);
                    retired_maps_.push(map_);
                    map_ = map;
                    map_capacity_ *= BlocksGrowFactor;
                    map_->blocks[map_size_++] = new block(); // allocate<block>(1);
                    goto insert;
                }
            } else {
                map_ = (block_map*)std::malloc(sizeof(block_map) + sizeof(block*) * BlocksGrowFactor);
                map_->blocks[0] = new block(); //allocate<block>(1);
                map_size_ = 1;
                map_capacity_ = BlocksGrowFactor;
                goto insert;
            }
        }

        size_t push_back(const T& value) { return emplace_back(value); }
        size_t push_back(T&& value) { return emplace_back(std::move(value)); }
    };
}

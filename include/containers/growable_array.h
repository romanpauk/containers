//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <array>
#include <cassert>
#include <memory>
#include <mutex>
#include <vector>

namespace containers {
    template< typename T, size_t BlockByteSize = 4096 > class growable_array {
        struct block {
            static constexpr size_t capacity() {
                static_assert(BlockByteSize >= sizeof(size_t));
                return std::max(size_t((BlockByteSize - sizeof(size_t)) / sizeof(T)), size_t(1));
            }

            ~block() {
                if (size_ > 0) {
                    do {
                        at(--size_)->~T();
                    } while(size_);
                }
            }

            template< typename Ty > void push_back(Ty&& value) {
                assert(size_ < capacity());
                new (at(size_++)) T(std::forward<Ty>(value));
            }

            T& operator[](size_t n) {
                assert(n < size_);
                return *at(n);
            }

            size_t size() const { return size_; }

        private:
            T* at(size_t n) {
                uint8_t* ptr = storage_.data() + n * sizeof(T);
                assert((reinterpret_cast<uintptr_t>(ptr) & (alignof(T) - 1)) == 0);
                return reinterpret_cast<T*>(ptr);
            }

            std::array<uint8_t, sizeof(T) * capacity()> storage_;
            size_t size_ = 0;
        };

        block* allocate_block() {
            // TODO: allocator
            return new (std::align_val_t(alignof(T))) block();
        }

        struct descriptor {
            std::vector< block* > blocks;
            size_t size = 0;
        };

        // TODO: atomic shared_ptr
        std::mutex desc_mutex_;
        std::shared_ptr< descriptor > desc_;

        struct reader_state {
            std::shared_ptr< descriptor > desc;
            size_t size;
        };

        static thread_local reader_state reader_;

    public:
        using value_type = T;

        ~growable_array() {
            if (desc_ && desc_->blocks.size() > 0) {
                auto& blocks = desc_->blocks;
                size_t i = blocks.size();
                do {
                    delete blocks[--i];
                } while(i);
            }
        }

        const T& operator[](size_t n) const {
            return const_cast<growable_array<T>&>(*this)->operator[](n);
        }

        T& operator[](size_t n) {
            // TODO: reader_ needs to be on caller's stack
            if (n >= reader_.size) {
                std::lock_guard lock(desc_mutex_);
                reader_.desc = desc_;
                reader_.size = reader_.desc->size;
            }

            assert(n < reader_.size);
            auto index = n / block::capacity();
            auto offset = n - index * block::capacity();
            return (*reader_.desc->blocks[index])[offset];
        }

        size_t size() const { return desc_ ? desc_->size : 0; }

        template< typename Ty > void push_back(Ty&& value) {
            if (!desc_) {
                auto desc = std::make_shared<descriptor>();
                desc->blocks.push_back(new block());
                desc->size = 0;
                desc_ = std::move(desc);
            } else {
                if (desc_->blocks.back()->size() == block::capacity()) {
                    if (desc_->blocks.size() == desc_->blocks.capacity()) {
                        auto desc = std::make_shared<descriptor>();
                        desc->blocks.reserve(desc_->blocks.size() * 1.5);
                        desc->blocks.insert(desc->blocks.begin(), desc_->blocks.begin(), desc_->blocks.end());
                        desc->size = desc_->size;

                        std::lock_guard lock(desc_mutex_);
                        desc_ = std::move(desc);
                    }

                    desc_->blocks.push_back(new block());
                }
            }

            desc_->blocks.back()->push_back(std::forward<Ty>(value));
            ++desc_->size;
        }
    };

    template< typename T, size_t BufferSize > thread_local typename growable_array< T, BufferSize >::reader_state growable_array< T, BufferSize >::reader_;
}

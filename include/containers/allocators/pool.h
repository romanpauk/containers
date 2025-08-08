//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <cassert>
#include <cstdint>
#include <cstring>

#include <algorithm>
#include <functional>

#include <immintrin.h>
#include <sys/mman.h>

#define __likely__(cond) __builtin_expect((cond), true)
#define __unlikely__(cond) __builtin_expect((cond), false)

namespace containers {
    template< std::size_t N, typename T = uint64_t > struct bitmap {
        static_assert((N & (N - 1)) == 0);

        bitmap() = default;

        bitmap(uint64_t value): values_{value}  {}

        void set(uint64_t v) {
            for(auto& value: values_)
                value = v;
        }

        void set_bit(std::size_t i) {
            values_[i/sizeof(T)/8] |= (T{1} << (i & (sizeof(T) * 8 - 1)));
        }

        const std::array<T, N / sizeof(T) >& get() const { return values_; }

        bool get_bit(std::size_t i) const {
            return values_[i/sizeof(T)/8] & (T{1} << (i & (sizeof(T) * 8 - 1)));
        }

        void clear_bit(uint64_t i) {
            values_[i/sizeof(T)/8] &= ~(T{1} << (i & (sizeof(T) * 8 - 1)));
        }

        uint64_t popcnt() const {
            uint64_t cnt = 0;
            for (auto value: values_) {
                cnt += _mm_popcnt_u64(value);
            }
            return cnt;
        }

        uint64_t tzcnt() const {
            uint64_t cnt = 0;
            for(std::size_t i = 0; i < values_.size(); ++i) {
                auto tmp = _tzcnt_u64(~values_[i]);
                cnt += tmp;
                if (tmp < sizeof(T) * 8)
                    break;
            }

            return cnt;
        }

        static constexpr std::size_t size() { return N; }

    private:
        std::array<T, N / sizeof(T)> values_ = {0};
    };

    template<> struct bitmap<64, uint64_t> {
        bitmap() = default;

        bitmap(uint64_t value): value_(value) {}

        void set(uint64_t value) {
            value_ = value;
        }

        void set_bit(uint64_t i) {
            assert(i < size());
            value_ |= (uint64_t(1) << i);
        }

        uint64_t get() const { return value_; }

        uint64_t get_bit(uint64_t i) const {
            assert(i < size());
            return (value_ >> i) & 1;
        }

        void clear_bit(uint64_t i) {
            assert(i < size());
            value_ &= ~(uint64_t(1) << i);
        }

        uint64_t popcnt() const {
            return _mm_popcnt_u64(value_);
        }

        uint64_t tzcnt() const {
            return _tzcnt_u64(~value_);
        }

        static constexpr std::size_t size() { return 64; }

    private:
        uint64_t value_;
    };

    template<typename T> struct heap {
        void push(T value) {
            assert(value != 0);
            values_.push_back(value);
            std::push_heap(values_.begin(), values_.end(), std::greater<T>());
        }

        T pop() {
            if (values_.empty())
                return T();
            std::pop_heap(values_.begin(), values_.end(), std::greater<T>());
            T value = values_.back();
            values_.pop_back();
            return value;
        }

    private:
        std::vector<T> values_;
    };

    struct memory_buffer_builder {
        memory_buffer_builder() = default;

        template<typename T, std::size_t Alignment = alignof(T)> void add() {
            size_ += sizeof(T) + Alignment - 1;
        }

        std::size_t size() const { return size_; }
    private:
        std::size_t size_ = 0;
    };

    struct memory_buffer_allocator {
        memory_buffer_allocator(void* buffer, std::size_t size)
            : begin_((uintptr_t)buffer)
            , current_(begin_)
            , size_(size)
        {}

        template<typename T, std::size_t Alignment = alignof(T)> T* allocate() {
            current_ = (current_ + Alignment - 1) & ~(Alignment - 1);
            if (current_ + sizeof(T) > begin_ + size_)
                std::abort();

            T* ptr = (T*)current_;
            current_ += sizeof(T);
            return ptr;
        }

    private:
        uintptr_t begin_;
        uintptr_t current_;
        std::size_t size_;
    };

    template<std::size_t N> struct PageChunkSize {
        static_assert((N & (N - 1)) == 0);
        static_assert(N >= 1024 && N <= 65536);
        static constexpr std::size_t index = N >> 11;
        static constexpr std::size_t size = 65536 / N;
    };

    template<typename T> struct PoolAllocatorState {
        using ChunkSize = PageChunkSize<1024>;

        uintptr_t chunk_ptr;
        bitmap<64>* page_chunk_element_bitmap;

        uint64_t page;
        uint64_t chunk;

        // bitmap<64>* page_chunk_bitmap
    };

    //
    // PageGroup, 4Mb
    //      group of 64 Pages
    //      managed by PageGroupManager
    // Page, 64k
    //      group of PageChunks
    // PageChunk, 1kb - 64kb based on PageChunkSize
    //      group of PageChunks
    //
    //
    // pool_allocator<T> will allocate T's from chunks allocated from
    //  its own page (as each page can contain only one size of chunks).
    //

    struct PageGroupDescriptor {
        bitmap<64> page_bitmap;

        //
        // TODO: will need some page state:
        //  thread id, live/dead etc.
        //
        std::array<bitmap<64>, 7> page_size_bitmaps;
        std::array<bitmap<64>, 64> page_chunk_bitmaps;
        std::array<std::array<bitmap<64>, 64>, 64> page_chunk_element_bitmaps;
    };

    template<std::size_t Size> struct PageGroupManager {
        static constexpr std::size_t PageGroupSize = 1 << 22;
        static constexpr std::size_t PageGroupCount = Size / PageGroupSize;
        static constexpr std::size_t PageSize = PageGroupSize / 64;
        static constexpr std::size_t PageCount = PageGroupCount * 64;

        void *memory_;
        std::size_t memory_size_;

        using PageGroupDescriptors = std::array<PageGroupDescriptor, PageGroupCount>;
        PageGroupDescriptors* page_group_descriptors_;

        using PageGroupLiveset = bitmap<PageGroupCount>;
        PageGroupLiveset* page_group_liveset_;

        using PageGroupDeadset = bitmap<PageGroupCount>;
        PageGroupDeadset* page_group_deadset_;

        using PageGroups = std::array<std::array<std::array<uint8_t, PageSize>, 64>, PageGroupCount>;
        PageGroups* page_groups_;
        std::size_t page_groups_index_ = 0;

        uint64_t page_group_full_;

        PageGroupManager() {
            memory_buffer_builder builder;
            builder.add<PageGroupDescriptors>();
            builder.add<PageGroupLiveset>();
            builder.add<PageGroupDeadset>();
            builder.add<PageGroups, PageGroupSize>();

            memory_size_ = builder.size();
            memory_ = mmap(0, memory_size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

            memory_buffer_allocator allocator(memory_, memory_size_);
            page_group_descriptors_ = allocator.allocate<PageGroupDescriptors>();
            page_group_liveset_ = allocator.allocate<PageGroupLiveset>();
            page_group_deadset_ = allocator.allocate<PageGroupDeadset>();
            page_groups_ = allocator.allocate<PageGroups, PageGroupSize>();

            allocate_page_zero();
        }

        void allocate_page_zero() {
            ++page_groups_index_;
            auto& descriptor = (*page_group_descriptors_)[0];
            memset(&descriptor, -1, sizeof(descriptor));
        }

        ~PageGroupManager() {
            munmap(memory_, memory_size_);
        }

        // Single-threaded
        template<typename T> T* allocate(PoolAllocatorState<T>& state) {
        again:
            auto* bitmap = state.page_chunk_element_bitmap;
            auto index = bitmap->tzcnt();
            if (__likely__(index < bitmap->size())) {
                bitmap->set_bit(index);
                // TODO: this hardcodes 16 bytes
                return (T*)(state.chunk_ptr + 16 * index);
            } else {
                if (allocate_update_chunk(state))
                    goto again;

                if (allocate_update_page(state))
                    goto again;

                std::abort();
                return nullptr;
            }
        }

        template<typename T> bool allocate_update_chunk(PoolAllocatorState<T>& state) {
            uintptr_t dist = (state.chunk_ptr - (uintptr_t)page_groups_);
            auto group = dist/PageGroupSize;
            auto page = dist/PageSize & 63;

            // TODO: scan existing PageChunks for non-full ones
            auto& descriptor = (*page_group_descriptors_)[group];
            auto& chunk_bitmap = descriptor.page_chunk_bitmaps[page];
            auto chunk = chunk_bitmap.tzcnt();
            if (__likely__(chunk < chunk_bitmap.size())) {
                chunk_bitmap.set_bit(chunk);
                setup_allocator_state(state, group, page, chunk);
                return true;
            } else {
                return false;
            }
        }

        // Multi-threaded
        // TODO: handle races, need a while()
        template<typename T> bool allocate_update_page(PoolAllocatorState<T>& state) {
            using ChunkSize = typename PoolAllocatorState<T>::ChunkSize;
            {
                auto& liveset = *page_group_liveset_;
                auto group = liveset.tzcnt();
                if (group < liveset.size()) {
                    auto& descriptor = (*page_group_descriptors_)[group];
                    auto& page_size_bitmap = descriptor.page_size_bitmaps[ChunkSize::index];

                    auto page_value = page_size_bitmap.get();
                    for (std::size_t page = 0; page < 64; ++page) {
                        // TODO: page is per-thread
                        if ((page_value >> page) & 1) {
                            auto chunk_value = descriptor.page_chunk_bitmaps[page].get();
                            for (std::size_t chunk = 0; chunk < 64; ++chunk) {
                                if ((chunk_value >> chunk) & 1) {

                                    if (descriptor.page_chunk_element_bitmaps[page][chunk].get() != (uint64_t)-1) {
                                        setup_allocator_state(state, group, page, chunk);
                                        return true;
                                    }
                                } else {
                                    descriptor.page_chunk_bitmaps[page].set_bit(chunk);
                                    setup_allocator_state(state, group, page, chunk);
                                    return true;
                                }
                            }
                        }
                    }
                }
            }

            if (page_groups_index_ < PageGroupCount) {
                auto group = page_groups_index_++;
                auto& descriptor = (*page_group_descriptors_)[group];
                descriptor.page_bitmap.set_bit(1);
                descriptor.page_size_bitmaps[ChunkSize::index].set_bit(1);
                descriptor.page_chunk_element_bitmaps[1][1].set_bit(1);
                setup_allocator_state(state, group, 1, 1);
                return true;
            } else {
                std::abort();
            }
        }

        template<typename T> void setup_allocator_state(PoolAllocatorState<T>& state, uint64_t group, uint64_t page, uint64_t chunk) {
            using ChunkSize = typename PoolAllocatorState<T>::ChunkSize;
            auto& descriptor = (*page_group_descriptors_)[group];
            state.chunk_ptr = (uintptr_t)&(*page_groups_)[group][page] + chunk * ChunkSize::size;
            state.page_chunk_element_bitmap = &descriptor.page_chunk_element_bitmaps[page][chunk];
            state.page = page;
            state.chunk = chunk;
        }

        template<typename T> void init_allocator_state(PoolAllocatorState<T>& state) {
            state.chunk = 0;
            state.chunk_ptr = 0;
            state.page = 0;
            state.page_chunk_element_bitmap = &(*page_group_descriptors_)[0].page_chunk_element_bitmaps[0][0];
        }

        template<typename T> void deallocate(PoolAllocatorState<T>& state, T* ptr) {
            using ChunkSize = typename PoolAllocatorState<T>::ChunkSize;

            uintptr_t dist = ((uintptr_t)ptr - (uintptr_t)page_groups_);
            auto group = dist/PageGroupSize;
            auto page = dist/PageSize & 63;
            auto chunk = dist/ChunkSize::size & ((PageSize/ChunkSize::size)-1);
            auto index = (uint64_t)ptr - (uint64_t)(*page_groups_)[group][page][chunk];

            auto& descriptor = (*page_group_descriptors_)[group];
            descriptor.page_chunk_element_bitmaps[page][chunk].clear_bit(index);
            if (descriptor.page_chunk_element_bitmaps[page][chunk].get() == 0) {
                if (descriptor.page_chunk_bitmaps[page].clear_bit(chunk)) {
                }
            }

            // TODO:
            // If empty,
            //  Deallocates chunk from page
            //  If empty,
            //   Deallocates page from group (places group on live set)
        }
    };

    template<typename T, typename PageGroupManagerT> struct pool_allocator {
        pool_allocator(PageGroupManagerT& manager)
            : manager_(manager)
        {
            manager_.init_allocator_state(state_);
        }

        T* allocate(std::size_t n) {
            assert(n == 1);(void)n;
            return manager_.allocate(state_);
        }

        void deallocate(T* ptr, std::size_t n) {
            assert(n == 1);(void)n;
            manager_.deallocate(state_, ptr);
        }

        PageGroupManagerT& manager_;
        PoolAllocatorState<T> state_;
    };
}


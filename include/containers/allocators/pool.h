//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <algorithm>
#include <functional>
#include <iostream>

#include <immintrin.h>
#include <sys/mman.h>

#define __likely__(cond) __builtin_expect((cond), true)
#define __unlikely__(cond) __builtin_expect((cond), false)

// #define DEBUG
// #define STATS

// #define PROT // Memory protection

constexpr const char* basefilename(const char* path) {
    const char* file = path;
    while (*path) {
        if (*path++ == '/') {
            file = path;
        }
    }
    return file;
}

#if defined(DEBUG)
#define __debug__(...) do { fprintf(stderr, "%s: %d: ", basefilename(__FILE__), __LINE__); fprintf(stderr, __VA_ARGS__); } while(0)
#else
#define __debug__(...)
#endif

#if defined(STATS)
#define __stats__(...) do { __VA_ARGS__; } while(0)
#else
#define __stats__(...)
#endif

namespace containers {
    template< std::size_t N, typename T = uint64_t > struct bitmap {
        static_assert((N & (N - 1)) == 0);

        bitmap() = default;

        bitmap(uint64_t value)  { set(value); }

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
                auto tmp = _tzcnt_u64(values_[i]);
                cnt += tmp;
                if (tmp < sizeof(T) * 8)
                    break;
            }

            return cnt;
        }

        uint64_t ffz(uint64_t begin = 0, uint64_t end = -1) const {
            uint64_t cnt = begin * 64;
            uint64_t j = std::min(values_.size(), end);
            for(std::size_t i = begin; i < j; ++i) {
                auto tmp = _tzcnt_u64(~values_[i]);
                cnt += tmp;
                if (tmp < sizeof(T) * 8)
                    break;
            }

            return cnt;
        }

        uint64_t ffz64(uint64_t n = 0) const {
            uint64_t cnt = n * 64;
            for(std::size_t i = n; i < values_.size(); ++i) {
                auto tmp = _tzcnt_u64(~values_[i]);
                cnt += tmp;
                if (tmp < sizeof(T) * 8)
                    break;
            }

            return cnt;
        }
        static constexpr std::size_t size() { return N; }

        static constexpr std::size_t size64() { return N / sizeof(T); }
        uint64_t get64(std::size_t index) const { return values_[index]; }

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
            return _tzcnt_u64(value_);
        }

        uint64_t ffz() const {
            return _tzcnt_u64(~value_);
        }

        static constexpr std::size_t size() { return 64; }

    private:
        uint64_t value_ = 0;
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

        template<typename T, std::size_t Alignment = alignof(T)> std::size_t add() {
            return size_ += sizeof(T) + Alignment - 1;
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

    template <typename T, typename = std::enable_if_t<std::is_unsigned_v<T>> >
    constexpr T RoundUp(T n) {
        T clz = 0;
        if constexpr (sizeof(T) <= 32)
          clz = __builtin_clzl(n-1);
        else if (sizeof(T) <= 64)
          clz = __builtin_clzll(n-1);
        else
            static_assert(false);

        return T{1} << (8 * sizeof(T) - clz);
    }

    template<std::size_t ClassSize> struct ClassMetadata {
        // TODO:
        static constexpr uint64_t page_size = 65536;
        static constexpr uint64_t page_count = 64;

        static constexpr uint64_t class_size = ClassSize;
        static_assert(class_size <= 1024);
        static_assert((class_size & (class_size - 1)) == 0);

        static constexpr uint64_t chunk_size = 64 * class_size;
        static constexpr uint64_t index = chunk_size >> 11;

        static constexpr uint64_t chunk_count = page_size / chunk_size;
        static_assert((chunk_count & (chunk_count - 1)) == 0);

        static constexpr uint64_t class_count = chunk_size / class_size;
        static_assert((class_count & (class_count - 1)) == 0);

        // TODO: is this intentional?
        static_assert(class_count == 64);
        static constexpr uint64_t class_count_mask = -1;
    };

    template<typename T> using ClassMetadataType = ClassMetadata< RoundUp(std::max(sizeof(T), sizeof(uint64_t) * 2)) >;

    struct PoolAllocatorState {
        uintptr_t chunk_ptr;
        bitmap<64>* chunk_elements_bitmap;

        uint64_t group;
        uint64_t page;
        uint64_t chunk;
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
    // pool_allocator<T> will allocate T's from chunks allocated from
    //  its own page (as each page can contain only one size of chunks).
    //

    struct PageGroupDescriptor {
        static constexpr std::size_t N = 14;
        //
        // TODO: will need some page state:
        //  thread id, live/dead etc.
        //
        uint64_t thread_id;

        uint64_t state;
        bitmap<64> page_bitmap;
        std::array<bitmap<64>, N> page_size_bitmaps;

        // TODO: need a really quick way to find free chunk in the whole group
        // for random access benchmarks
        std::array<bitmap<64*64>, N> page_live_chunks_bitmaps;

        std::array<bitmap<64>, 64> page_chunk_bitmaps;
        std::array<std::array<bitmap<64>, 64>, 64> page_chunk_elements_bitmaps;
    };

    struct thread_id {
        static uint64_t get() { return (uint64_t)&id_; }

    private:
        static constexpr uint64_t id_ = 0;
    };

    struct PageGroupManagerStats {
        PageGroupManagerStats() = default;

        uint64_t allocate_update_chunk[2] = {0};
        uint64_t allocate_update_page = 0;
        uint64_t allocate_update_page_used_page = 0;
        uint64_t allocate_update_page_used_chunk = 0;
        uint64_t allocate_update_page_used_chunk_iteration = 0;
        uint64_t allocate_update_page_free_chunk = 0;
        uint64_t allocate_update_page_free_page = 0;
        uint64_t allocate_update_page_full_page = 0;
        uint64_t allocate_update_group = 0;
        uint64_t allocate_update_group_full_group = 0;
        uint64_t allocate_update_group_used_group = 0;
        uint64_t allocate_update_group_new_group = 0;
    };

    std::ostream& operator << (std::ostream& stream, const PageGroupManagerStats& stats) {
        return stream
            << "chunk " << (double)stats.allocate_update_chunk[0] / stats.allocate_update_chunk[1] << " (" << stats.allocate_update_chunk[1] << ")"
            << " used page " << (double)stats.allocate_update_page_used_page / stats.allocate_update_page << " (" << stats.allocate_update_page_used_page << ")"
            << " used chunk " << (double)stats.allocate_update_page_used_chunk / stats.allocate_update_page << " (" << stats.allocate_update_page_used_chunk << ")"
            // << " used chunk iteration " << (double)stats.allocate_update_page_used_chunk / stats.allocate_update_page_used_chunk_iteration
            << " free chunk " << (double)stats.allocate_update_page_free_chunk / stats.allocate_update_page << " (" << stats.allocate_update_page_free_chunk << ")"
            << " free page " << (double)stats.allocate_update_page_free_page / stats.allocate_update_page << " (" << stats.allocate_update_page_free_page << ")"
            << " full page " << (double)stats.allocate_update_page_full_page / stats.allocate_update_page << " (" << stats.allocate_update_page_full_page << ")"
            << " used group " << (double)stats.allocate_update_group_used_group / (stats.allocate_update_group + 1) << " (" << stats.allocate_update_group_used_group << ")"
            << " new group " << (double)stats.allocate_update_group_new_group / (stats.allocate_update_group + 1) << " (" << stats.allocate_update_group_new_group << ")"
            << " full group " << (double)stats.allocate_update_group_full_group
            ;
    }

    template<std::size_t Size> struct PageGroupManager {
        static constexpr uint64_t PageGroupSize = 1 << 22;
        static constexpr uint64_t PageGroupCount = Size / PageGroupSize;
        static constexpr uint64_t PageSize = PageGroupSize / 64;
        static constexpr uint64_t PageCount = PageGroupCount * 64;

        void *memory_;
        std::size_t memory_size_;

        using PageGroupDescriptors = std::array<PageGroupDescriptor, PageGroupCount>;
        PageGroupDescriptors* page_group_descriptors_;

        using PageGroupLiveset = bitmap<PageGroupCount>;
        PageGroupLiveset* page_group_liveset_;

        // TODO: right now, there is no use for deadset, as free pages
        // are simply kept in liveset. Not sure where their deallocated state will be
        // tracked.
        using PageGroupDeadset = bitmap<PageGroupCount>;
        PageGroupDeadset* page_group_deadset_;

        using PageGroups = std::array<std::array<std::array<uint8_t, PageSize>, 64>, PageGroupCount>;
        PageGroups* page_groups_;
        std::size_t page_groups_index_ = 0;

        PageGroupManagerStats* stats_ = nullptr;

        static bitmap<64> default_bitmap_;

        PageGroupManager(PageGroupManagerStats* stats = nullptr)
        #if defined(STATS)
            : stats_(stats)
        #endif
        {
            (void)stats;
            memory_buffer_builder builder;
            builder.add<PageGroupDescriptors>();
            builder.add<PageGroupLiveset, 4096>();
            builder.add<PageGroupDeadset, 4096>();
            builder.add<PageGroups, PageGroupSize>();

            memory_size_ = builder.size();
        #if defined(PROT)
            memory_ = mmap(0, memory_size_, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        #else
            memory_ = mmap(0, memory_size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        #endif

            memory_buffer_allocator allocator(memory_, memory_size_);
            page_group_descriptors_ = allocator.allocate<PageGroupDescriptors>();
            page_group_liveset_ = allocator.allocate<PageGroupLiveset, 4096>();
            page_group_deadset_ = allocator.allocate<PageGroupDeadset, 4096>();
            page_groups_ = allocator.allocate<PageGroups, PageGroupSize>();

        #if defined(PROT)
            protect(page_group_descriptors_, sizeof(PageGroupDescriptors), PROT_READ | PROT_WRITE);
            protect(page_group_liveset_, sizeof(PageGroupLiveset), PROT_READ | PROT_WRITE);
        #endif

            allocate_group_zero();
        }

        void allocate_group_zero() {
            protect_group(0, PROT_NONE);
            // TODO: descriptor protection

            ++page_groups_index_;
            auto& descriptor = (*page_group_descriptors_)[0];
            memset((void*)&descriptor, -1, sizeof(descriptor));
        }

        ~PageGroupManager() {
            munmap(memory_, memory_size_);
        }

        void protect_group(uint64_t group, int prot) {
        #if defined(PROT)
            protect(&(*page_groups_)[group], PageGroupSize, prot);
            if ((prot & PROT_READ) || (prot & PROT_WRITE))
                madvise(&(*page_groups_)[group], PageGroupSize, MADV_WILLNEED);
            if (prot == PROT_NONE)
                madvise(&(*page_groups_)[group], PageGroupSize, MADV_DONTNEED);
        #endif
        }

        void protect(void* ptr, std::size_t size, int prot) {
        #if defined(PROT)
            if (mprotect(ptr, size, prot) != 0) {
                std::abort();
            }
        #endif
        }

        // Single-threaded
        template<typename Metadata> void* allocate(PoolAllocatorState& state) {
        again:
            auto* bitmap = state.chunk_elements_bitmap;
            auto index = bitmap->ffz();
            if (__likely__(index < Metadata::class_count)) {
                bitmap->set_bit(index);
                void* p = (void*)(state.chunk_ptr + Metadata::class_size * index);

                __debug__("allocate() %p group %lu page %lu chunk %lu index %lu\n",
                    p, state.group, state.page, state.chunk, index);

                return p;
            } else {
                // Skip zero group as that is full by definition
                if (__likely__(state.group > 0)) {
                    auto& descriptor = (*page_group_descriptors_)[state.group];
                    __debug__("clear bit page %lu chunk %lu\n", state.page, state.chunk);
                    descriptor.page_live_chunks_bitmaps[Metadata::index].clear_bit(state.page * Metadata::chunk_count + state.chunk);

                    // Try to find usable chunk in current page
                    if (__likely__(allocate_update_chunk<Metadata>(state)))
                        goto again;

                    // Try to find usable chunk in current group
                    if (allocate_update_page<Metadata>(state, state.group))
                        goto again;
                }

                // Try to find usable chunk in some other group
                if (allocate_update_group<Metadata>(state))
                    goto again;

                std::abort();
                return nullptr;
            }
        }

        // Single-threaded, page is used by owning thread only
        template<typename Metadata> bool allocate_update_chunk(PoolAllocatorState& state) {
            uintptr_t dist = (state.chunk_ptr - (uintptr_t)page_groups_);
            auto group = dist/PageGroupSize;
            auto page = dist/PageSize & (Metadata::chunk_count - 1);

            __stats__(++stats_->allocate_update_chunk[1];);

            auto& descriptor = (*page_group_descriptors_)[group];
            auto& chunk_bitmap = descriptor.page_chunk_bitmaps[page];
            auto chunk = chunk_bitmap.ffz();
            if (__likely__(chunk < Metadata::chunk_count)) {
                __stats__(++stats_->allocate_update_chunk[0];);
                chunk_bitmap.set_bit(chunk);
                __debug__("set bit page %lu chunk %lu\n", page, chunk);
                descriptor.page_live_chunks_bitmaps[Metadata::index].set_bit(page * Metadata::chunk_count + chunk);
                setup_allocator_state<Metadata>(state, group, page, chunk);

                __debug__("allocate_update_chunk() group %lu page %lu chunk %lu\n",
                    state.group, state.page, state.chunk);

                return true;
            } else {
                return false;
            }
        }

        // Multi-threaded
        // TODO: handle races, need a while()
        template<typename Metadata> bool allocate_update_page(PoolAllocatorState& state, uint64_t group) {
            // Try current group first
            auto& descriptor = (*page_group_descriptors_)[group];
#if 1
            __stats__(++stats_->allocate_update_page;);

            const auto& live_chunks_bitmap = descriptor.page_live_chunks_bitmaps[Metadata::index];
            for (uint64_t i = 0; i < live_chunks_bitmap.size64(); ++i) {
                auto bit = (uint64_t)_tzcnt_u64(live_chunks_bitmap.get64(i));
                if (bit < 64) {   // Note: we really iterate 64bit values here
                    bit += i * 64;
                    auto page = bit / Metadata::chunk_count;
                    auto chunk = bit % Metadata::chunk_count;
                    __debug__("checking page %lu, chunk %lu, bit %lu\n", page, chunk, bit);
                    assert(descriptor.page_live_chunks_bitmaps[Metadata::index].get_bit(page * Metadata::chunk_count + chunk) == 1);
                    assert(descriptor.page_chunk_elements_bitmaps[page][chunk].get() != (uint64_t)-1);
                    setup_allocator_state<Metadata>(state, group, page, chunk);
                    return true;
                }
            }
#endif
            // Look for an used page with a free chunk that can be reused by this size
            auto& page_size_bitmap = descriptor.page_size_bitmaps[Metadata::index];
            auto page_size_value = page_size_bitmap.get();

            // https://lemire.me/blog/2018/02/21/iterating-over-set-bits-quickly/
            while (page_size_value != 0) {
                uint64_t p = page_size_value & -page_size_value;
                uint64_t page = __builtin_ctzl(page_size_value);
                page_size_value ^= p;

                // TODO: page is per-thread, need to check if it is assigned to one
                if (page == state.page)
                    continue;

                assert(descriptor.page_bitmap.get_bit(page) == 1);

                uint64_t chunk_value = 0, chunk = 0;

                // Iterate free chunks
                chunk = descriptor.page_chunk_bitmaps[page].ffz();
                if (chunk < Metadata::chunk_count) {
                    __stats__(
                        ++stats_->allocate_update_page_free_chunk;
                        ++stats_->allocate_update_page_used_page;
                    );

                    assert(descriptor.page_chunk_elements_bitmaps[page][chunk].get() == 0);
                    descriptor.page_chunk_bitmaps[page].set_bit(chunk);

                    // This chunk is not live
                    assert(descriptor.page_live_chunks_bitmaps[Metadata::index].get_bit(page * Metadata::chunk_count + chunk) == 0);
                    __debug__("clear bit %lu\n", chunk);
                    descriptor.page_live_chunks_bitmaps[Metadata::index].set_bit(page * Metadata::chunk_count + chunk);
                    setup_allocator_state<Metadata>(state, state.group, page, chunk);
                    return true;
                }
#if 0
                //
                // TODO: already iterated in live chunks
                //
                // Iterate chunks in use
                chunk_value = descriptor.page_chunk_bitmaps[page].get();
                while (chunk_value != 0) {
                    uint64_t c = chunk_value & -chunk_value;
                    chunk = __builtin_ctzl(chunk_value);
                    chunk_value ^= c;

                    // __stats__(++stats_->allocate_update_page_used_chunk_iteration;);

                    assert(descriptor.page_chunk_bitmaps[page].get_bit(chunk) == 1);
                    // TODO:
                    if (descriptor.page_chunk_elements_bitmaps[page][chunk].get() != (uint64_t)-1) {
                        __stats__(
                            ++stats_->allocate_update_page_used_chunk;
                            ++stats_->allocate_update_page_used_page;
                        );

                        // This chunk is live by definition
                        assert(descriptor.page_live_chunks_bitmaps[Metadata::index].get_bit(page * Metadata::chunk_count + chunk) == 1);
                        setup_allocator_state<Metadata>(state, state.group, page, chunk);
                        return true;
                    }
                }
#endif
            }

            __stats__(++stats_->allocate_update_page_full_page;);

            // Iterate free pages
            auto page = descriptor.page_bitmap.ffz();
            if (page < Metadata::page_count) {
                __stats__(++stats_->allocate_update_page_free_page;);

                assert(descriptor.page_bitmap.get_bit(page) == 0);
                descriptor.page_bitmap.set_bit(page);
                descriptor.page_size_bitmaps[Metadata::index].set_bit(page);
                descriptor.page_chunk_bitmaps[page].set_bit(0);
                __debug__("set bit page %lu chunk 0\n", page);
                descriptor.page_live_chunks_bitmaps[Metadata::index].set_bit(page * Metadata::chunk_count + 0);
                setup_allocator_state<Metadata>(state, state.group, page, 0);
                return true;
            }

            return false;
        }

        template<typename Metadata> bool allocate_update_group(PoolAllocatorState& state) {
            __stats__(++stats_->allocate_update_group;);
#if 1
            for (std::size_t i = 1; i < page_group_liveset_->size64(); ++i) {
                uint64_t value = page_group_liveset_->get64(i);
                while(value) {
                    uint64_t tmp = value & -value;
                    uint64_t group = i * 64 + __builtin_ctzl(value);
                    value ^= tmp;

                    if (allocate_update_page<Metadata>(state, group)) {
                        __stats__(++stats_->allocate_update_group_used_group;);
                        return true;
                    } else {
                        __stats__(++stats_->allocate_update_group_full_group;);
                    }
                }

                if (i * 64 > page_groups_index_)
                    break;
            }
#endif
            if (page_groups_index_ < PageGroupCount) {
                auto group = page_groups_index_++;
                auto& descriptor = (*page_group_descriptors_)[group];
                descriptor.thread_id = thread_id::get();
                descriptor.page_bitmap.set_bit(0);
                descriptor.page_size_bitmaps[Metadata::index].set_bit(0);
                descriptor.page_chunk_bitmaps[0].set_bit(0);
                __debug__("set bit page 0 chunk 0\n");
                descriptor.page_live_chunks_bitmaps[Metadata::index].set_bit(0);

                // TODO: the liveset is somehow abandoned
                (*page_group_liveset_).set_bit(group);

                __stats__(++stats_->allocate_update_group_new_group;);

                setup_allocator_state<Metadata>(state, group, 0, 0);
                protect_group(group, PROT_READ | PROT_WRITE);
                return true;
            } else {
                std::abort();
            }
        }

        template<typename Metadata> void setup_allocator_state(PoolAllocatorState& state, uint64_t group, uint64_t page, uint64_t chunk) {
            assert(group > 0);
            assert(group > 0);
            assert(page < Metadata::page_count);
            assert(chunk < Metadata::chunk_count);

            auto& descriptor = (*page_group_descriptors_)[group];
            state.chunk_ptr = (uintptr_t)&(*page_groups_)[group][page] + chunk * Metadata::chunk_size;
            state.chunk_elements_bitmap = &descriptor.page_chunk_elements_bitmaps[page][chunk];
            state.page = page;
            state.chunk = chunk;
            state.group = group;
        }

        static PoolAllocatorState init_allocator_state() {
            PoolAllocatorState state;
            state.chunk = 0;
            state.chunk_ptr = 0;
            state.page = 0;
            state.group = 0;
            state.chunk_elements_bitmap = &default_bitmap_;
            return state;
        }

        template<typename Metadata> void deallocate(PoolAllocatorState& state, void* ptr) {
            // TODO: this is stupid
            uintptr_t address = (uintptr_t)ptr;
            auto group = (address - (uintptr_t)page_groups_) / PageGroupSize;
            assert(group > 0);
            auto page = (address - (uintptr_t)&(*page_groups_)[group]) / PageSize;
            auto chunk = (address - (uintptr_t)&(*page_groups_)[group][page]) / Metadata::chunk_size;
            // TODO: hardcodes 16bytes
            auto index = (address - ((uint64_t)&(*page_groups_)[group][page] + chunk * Metadata::chunk_size)) / Metadata::class_size;

            auto& descriptor = (*page_group_descriptors_)[group];
            if (descriptor.thread_id == thread_id::get()) {
                auto& elements_bitmap = descriptor.page_chunk_elements_bitmaps[page][chunk];
                auto elements_count = elements_bitmap.popcnt();

                __debug__("deallocate() %p group %lu page %lu chunk %lu index %lu elements %lu\n",
                    ptr, group, page, chunk, index, elements_count);

                assert(elements_bitmap.get_bit(index));
                elements_bitmap.clear_bit(index);

                if (elements_count == Metadata::class_count) {
                    // This is a first deallocation to fully allocated chunk
                    //
                    // Note: the chunk can be still live if there was not an allocation that would fail and remove it from live chunks
                    //assert(descriptor.page_live_chunks_bitmaps[Metadata::index].get_bit(page * Metadata::chunk_count + chunk) == 0);
                    __debug__("set bit page %lu chunk %lu\n", page, chunk);
                    descriptor.page_live_chunks_bitmaps[Metadata::index].set_bit(page * Metadata::chunk_count + chunk);
                } else if (elements_count == 1) {
                    // This is last deallocation to now empty chunk
                    if (descriptor.page_chunk_bitmaps[page].popcnt() == 1 && state.group == group) {
                        // This is deallocation of last chunk in cached page, bail out
                        return;
                    }

                    descriptor.page_chunk_bitmaps[page].clear_bit(chunk);
                    assert(descriptor.page_live_chunks_bitmaps[Metadata::index].get_bit(page * Metadata::chunk_count + chunk) == 1);
                    __debug__("clear bit page %lu chunk %lu\n", page, chunk);
                    descriptor.page_live_chunks_bitmaps[Metadata::index].clear_bit(page * Metadata::chunk_count + chunk);

                    if (descriptor.page_chunk_bitmaps[page].get() == 0) {
                        // Page is completely empty

                        // TODO: assert that all that should be empty is
                        // assert(descriptor.page_live_chunks_bitmaps[Metadata::index].popcnt() == 0);

                        descriptor.page_bitmap.clear_bit(page);
                        descriptor.page_size_bitmaps[Metadata::index].clear_bit(page);

                        if (descriptor.page_bitmap.get() == 0) {
                            assert(group != state.group);
                            protect_group(group, PROT_NONE);
                            page_group_liveset_->clear_bit(group);
                        }
                    }
                }
            } else {
                // TODO: non-owning thread path
                std::abort();
            }
        }
    };

    template<std::size_t Size> bitmap<64> PageGroupManager<Size>::default_bitmap_(-1);

    template<std::size_t Size> struct GlobalPageGroupManager {
        static constexpr uint64_t PageGroupSize = PageGroupManager<Size>::PageGroupSize;

        GlobalPageGroupManager(PageGroupManagerStats* = nullptr) {}

        template<typename Metadata> void* allocate() {
            return manager_.template allocate<Metadata>(state_);
        }

        template<typename Metadata> void deallocate(void* ptr) {
            return manager_.template deallocate<Metadata>(state_, ptr);
        }

    private:
        static PageGroupManager<Size> manager_;
        static thread_local PoolAllocatorState state_;
    };

    template<std::size_t Size> PageGroupManager<Size> GlobalPageGroupManager<Size>::manager_;
    template<std::size_t Size> thread_local PoolAllocatorState GlobalPageGroupManager<Size>::state_ = PageGroupManager<Size>::init_allocator_state();

    template<std::size_t Size> struct LocalPageGroupManager: PageGroupManager<Size> {
        LocalPageGroupManager(PageGroupManagerStats* stats = nullptr)
            : PageGroupManager<Size>(stats) {
            state_ = PageGroupManager<Size>::init_allocator_state();
        }

        template<typename Metadata> void* allocate() {
            return PageGroupManager<Size>::template allocate<Metadata>(state_);
        }

        template<typename Metadata> void deallocate(void* ptr) {
            return PageGroupManager<Size>::template deallocate<Metadata>(state_, ptr);
        }

    private:
        PoolAllocatorState state_;
    };

    template<typename T, typename PageGroupManagerT> struct pool_allocator {
        template <typename U1, typename U2, typename PageGroupManagerU>
        friend bool operator == (pool_allocator<U1, PageGroupManagerU> const&, pool_allocator<U2, PageGroupManagerU> const&) noexcept;

        using value_type = T;

        pool_allocator(PageGroupManagerT& manager)
            : manager_(manager)
        {}

        template <class U> pool_allocator(pool_allocator<U, PageGroupManagerT> const& other) noexcept
            : manager_(other.manager_)
        {}

        T* allocate(std::size_t n) {
            if constexpr (sizeof(T) <= 2048) {
                if (__likely__(n == 1))
                    return (T*)manager_.template allocate<ClassMetadataType<T>>();
            }

            std::allocator<T> alloc;
            return alloc.allocate(n);
        }

        void deallocate(T* ptr, std::size_t n) {
            if constexpr (sizeof(T) <= 2048) {
                if (__likely__(n == 1))
                    manager_.template deallocate<ClassMetadataType<T>>(ptr);
            }

            //std::allocator<T> alloc;
            //return alloc.deallocate(ptr, n);
        }

        PageGroupManagerT& manager_;
    };

    template <typename T, typename U, typename PageGroupManagerT>
    bool operator == (pool_allocator<T, PageGroupManagerT> const& lhs, pool_allocator<U, PageGroupManagerT> const& rhs) noexcept {
        // TODO: for global manager, this is always true
        return &lhs.manager_ = &rhs.manager_;
    }

    template <typename T, typename U, typename PageGroupManagerT>
    bool operator != (pool_allocator<T, PageGroupManagerT> const& x, pool_allocator<U, PageGroupManagerT> const& y) noexcept {
        return !(x == y);
    }

    template<typename T, std::size_t Size = 1ull << 32 > struct bump_allocator {
        // Jemalloc returns 8byte aligned memory,
        // lets do that too, at least in allocator<> where the type is known
        static constexpr std::size_t ClassSize = RoundUp(std::max(sizeof(T), sizeof(uint64_t)));
        static constexpr std::size_t Capacity = Size / ClassSize;
        static constexpr std::size_t PageCapacity = 64;

        bitmap<Capacity/PageCapacity/64>* pages_index_;
        bitmap<Capacity/PageCapacity>* pages_;
        std::array<bitmap<PageCapacity>, Capacity/PageCapacity>* bitmaps_;
        uint64_t page_ = 0;
        uint64_t pages_index_low_ = 0;

        bump_allocator() {
            memory_buffer_builder builder;
            builder.add<decltype(*pages_index_)>();
            builder.add<decltype(*pages_), 4096>();
            builder.add<decltype(*bitmaps_), 4096>();
            builder.add<std::array<uint8_t, ClassSize * Capacity>, Size>();

            size_ = builder.size();
            memory_ = mmap(0, size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

            memory_buffer_allocator allocator(memory_, size_);
            pages_index_ = allocator.allocate<std::decay_t<decltype(*pages_index_)>>();
            pages_ = allocator.allocate<std::decay_t<decltype(*pages_)>, 4096>();
            bitmaps_ = allocator.allocate<std::decay_t<decltype(*bitmaps_)>, 4096>();
            base_ = (uint64_t)allocator.allocate<std::array<uint8_t, ClassSize * Capacity>, Size>();
        }

        ~bump_allocator() {
            munmap(memory_, size_);
        }

        T* allocate(std::size_t n) {
            assert(n == 1); (void)n;

        again:
            auto index = (*bitmaps_)[page_].ffz();
            if (__likely__(index < bitmap<PageCapacity>::size())) {
                (*bitmaps_)[page_].set_bit(index);
                index += page_ * PageCapacity;
                T* p = (T*)(base_ + ClassSize * index);
                assert(get_index(p) == index);
                return p;
            } else {
                pages_->set_bit(page_);

                if (pages_->get64(page_ / 64) != -1) {
                    page_ = pages_->ffz(page_ / 64, page_ / 64 + 1);
                } else {
                    pages_index_->set_bit(page_ / 64);
                    auto pages_low = pages_index_->ffz(pages_index_low_);
                    pages_index_low_ = pages_low / 64;
                    page_ = pages_->ffz(pages_low);
                }

                goto again;
            }
        }

        void deallocate(T* p, std::size_t) {
            auto index = get_index(p);
            (*bitmaps_)[index/PageCapacity].clear_bit(index & (PageCapacity - 1));
            pages_->clear_bit(index/PageCapacity);

            if (pages_index_->get_bit(index/PageCapacity/64) == 1) {
                pages_index_->clear_bit(index/PageCapacity/64);
                pages_index_low_ = std::min(pages_index_low_, index/PageCapacity/64/64);
            }
        }

        uint64_t get_index(void* ptr) {
            return ((uint64_t)ptr & (Size - 1)) / ClassSize;
        }

    private:
        void* memory_;
        std::size_t size_ = 0;

        uint64_t base_;
    };
}


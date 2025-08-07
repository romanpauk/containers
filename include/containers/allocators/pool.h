//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <cassert>
#include <cstdint>

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

        uintptr_t chunk;
        bitmap<64>* bitmap;
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
        }

        ~PageGroupManager() {
            munmap(memory_, memory_size_);
        }

        // Single-threaded
        template<typename T> T* allocate(PoolAllocatorState<T>& state) {
            using ChunkSize = typename PoolAllocatorState<T>::ChunkSize;

        again:
            auto index = state.bitmap.tzcnt();
            if (__likely__(index < state.bitmap.size())) {
                state.bitmap.set_bit(index);
                return (T*)(state.chunk + sizeof(T) * index);
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
            using ChunkSize = typename PoolAllocatorState<T>::ChunkSize;

            uintptr_t dist = (state.chunk - (uintptr_t)page_groups_);
            auto group = dist/PageGroupSize;
            auto page = dist/PageSize & 63;

            // TODO: scan existing PageChunks for non-full ones
            auto& descriptor = (*page_group_descriptors_)[group];
            auto& chunk_bitmap = descriptor.page_chunk_bitmaps[page];
            auto chunk = chunk_bitmap.tzcnt();
            if (__likely__(chunk < chunk_bitmap.size())) {
                chunk_bitmap.set_bit(chunk);
                state.bitmap = &chunk_bitmap;
                state.chunk = (uintptr_t)&(*page_groups_)[group][page] + chunk * ChunkSize::value;
                state.chunk = 0;
                return true;
            } else {
                return false;
            }
        }

        // Multi-threaded
        // TODO: handle races, need a while()
        template<typename T> bool allocate_update_page(PoolAllocatorState<T>& state) {
            auto& liveset = *page_group_liveset_;
        again:
            auto group = liveset.tzcnt();
            if (group < liveset.size()) {
                auto& descriptor = &(*page_group_descriptors_)[group];
                auto page = allocate_page(descriptor);
                if (page != descriptor.page_bitmap.size()) {
                    state.group = group;
                    state.page = page;
                    return allocate_page_chunk(state);
                } else {
                    liveset.clear_bit(group);
                    goto again;
                }
            }

            if (page_groups_index_ < PageGroupCount) {
                state.group = page_groups_index_++;
                auto& descriptor = &(*page_group_descriptors_)[state.group];
                auto page = allocate_page(descriptor);
                if (page != descriptor.page_bitmap.size()) {
                    state.page = page;
                    return allocate_page_chunk(state);
                }
            } else {
                std::abort();
            }
        }

        bool allocate_page(PageGroupDescriptor& descriptor) {
            auto page = descriptor.page_bitmap.tzcnt();
            if (page < descriptor.page_bitmap.size()) {
                descriptor.page_bitmap.set_bit(page);
                return true;
            }
            return false;
        }

        template<typename T> void deallocate(PoolAllocatorState<T>& state, T* ptr) {
            using ChunkSize = typename PoolAllocatorState<T>::ChunkSize;

            uintptr_t dist = ((uintptr_t)ptr - (uintptr_t)page_groups_);
            auto group = dist/PageGroupSize;
            auto page = dist/PageSize & 63;
            auto chunk = dist/ChunkSize::size & ((PageSize/ChunkSize::size)-1);

            auto& descriptor = (*page_groups_desriptors_)[group];
            descriptor.page_chunk_element_bitmaps[page][chunk].set_bit(/*ptr to chunk element*/);

            // Get descriptor
            // Get chunk bitmap
            // Deallocates element from chunk bitmap
            // If empty,
            //  Deallocates chunk from page
            //  If empty,
            //   Deallocates page from group (places group on live set)
        }

#if 0
        template< typename ChunkSize > std::size_t allocate_page() {
            auto& liveset = (*page_group_liveset_)[ChunkSize::index].chunk_size_bitmaps[ChunkSize::index];
            auto page = liveset.tzcnt();
            if (page < liveset.size()) {
                return page;
            } else {
                page = page_deadset_->tzcnt();
                if (page == page_deadset_->size()) {
                    page = page_space_index_++;
                }
            }

            assert(page < PageCount);
            return page;
        }

        template< typename SizeClass > std::size_t allocate_chunk(std::size_t page) {
            auto& metadata = (*meta_space_)[page];
            return metadata.pagebits.tzcnt();
        }

        void* get_address(std::size_t page_index, std::size_t chunk_index) {
            return (*page_space_)[page_index][chunk_index];
        }

        std::size_t get_page_index(void* ptr) {
            auto page = ((uintptr_t)page - (uintptr_t)page_space_) / PageSize;
            assert(page < page_space->size());
            return page;
        }

        std::size_t get_chunk_index(void* ptr) {
        }

        template< typename SizeClass > void mark_live(void* ptr, bool) {
            auto page = get_page_index(ptr);
            // TODO: assert
            (*page_liveset_)[SizeClass::Index].set_bit(page);
        }

        void mark_dead(void* ptr, bool) {
            auto page = get_page_index(ptr);
            // TODO: assert
            (*page_deadset_)[SizeClass::Index].set_bit(page);
        }

        void deallocate(void* ptr) {
            auto page = get_page_index(ptr);
            auto chunk = get_chunk_index(ptr);
            (*meta_space_)[page];
            // Deallocate chunk
            //
            // If page is not cached and it is empty, deallocate it
            //  (write to deadset)
      }
#endif
    };

    template<typename T, typename PageGroupManagerT> struct PoolAllocator {
        PoolAllocator(PageGroupManagerT& manager)
            : manager_(manager)
        {
            manager_.init_pool_allocator_state(state_);
        }

        T* allocate() {
            return manager_.allocate(state_);
        }

        void deallocate(T* ptr) {
            manager_.deallocate(state_, ptr);
        }

        PageGroupManagerT& manager_;
        PoolAllocatorState<T> state_;
    };

    template<std::size_t Size, std::size_t Count> struct page_manager {
        static constexpr std::size_t PageSize = Size;
        static_assert((PageSize & (PageSize - 1)) == 0);
        static_assert(PageSize == 65536);
        static constexpr std::size_t PageCount = Count;

        page_manager() {
            mmap_base_ = (uint64_t)mmap(0, PageSize * (PageCount + 1), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            mmap_current_ = mmap_start_ = (mmap_base_ + PageSize - 1) & ~(PageSize - 1);
        }

        ~page_manager() {
            munmap((void*)mmap_base_, PageSize * (PageCount + 1));
        }

        template< std::size_t SizeClass > void* allocate() {
            assert((SizeClass & (SizeClass - 1)) == 0);

            void*& page = page_class_[SizeClass >> 11];
            if (__unlikely__(!page)) {
            again:
                page = allocate_page<SizeClass>();
                if (!page)
                    std::abort();
            }

            auto& metadata = page_metadata_[get_page_index(page)];
            auto index = metadata.tzcnt();
            if (index < metadata.size()) {
                return (void*)((uintptr_t)page + SizeClass * index);
            } else {
                goto again;
            }
        }

        template< std::size_t SizeClass > void* allocate_page() {
            void* ptr = nullptr;
            // TODO: this needs to be per SizeClass
            auto& liveset = page_class_live_[SizeClass >> 11];
            auto index = liveset.tzcnt();
            if (index < liveset.size()) {
                liveset.clear_bit(index);
                ptr = get_page(index);
            } else {
                ptr = (void*)mmap_current_;
                mmap_current_ += PageSize;
                if (mmap_current_ > mmap_start_ + PageSize * PageCount)
                    return nullptr;
            }

            assert(liveset.get_bit(get_page_index(ptr)) == 0);
            page_metadata_[get_page_index(ptr)].set(0);
            return (void*)ptr;
        }

        template< std::size_t SizeClass > void deallocate(void* ptr) {
            auto& metadata = page_metadata_[get_page_index(ptr)];
            assert(metadata.get_bit(get_page_class_index<SizeClass>(ptr)) == 1);
            metadata.set_bit(get_page_class_index<SizeClass>(ptr));
            if (metadata.get() == -1) {
                page_class_live_[SizeClass >> 11].clear_bit(get_page_index(ptr));
                deallocate_page(ptr);
            } else {
                page_class_live_[SizeClass >> 11].set_bit(get_page_index(ptr));
            }
        }

        uint64_t get_page_index(void* ptr) {
            auto index = ((uint64_t)ptr - mmap_start_) / PageSize;
            assert(index < PageCount);
            return index;
        }

        void* get_page_address(uint64_t index) {
            assert(index < PageCount);
            return (void*)(mmap_start_ + PageSize * index);
        }

    private:
        uint64_t mmap_base_;
        uint64_t mmap_start_;
        uint64_t mmap_current_;

        bitmap<PageCount> deallocated_pages_;

        // page_manager will allow allocation in steps of
        // 1024, 2048, 4096, 8192...
        // TODO: this is fixed now for page size of 65536
        bitmap<PageSize/1024> page_metadata_[PageCount] = {0};

        static std::array<void*, 7> page_class_;

        // Live pages available for allocation
        static std::array< bitmap<PageCount>, 7 > page_class_live_;
    };

    template<typename T, typename PageManager> struct pool_page_allocator {
        static constexpr std::size_t PageSize = PageManager::PageSize;
        static constexpr std::size_t PageCount = PageManager::PageCount;

        pool_page_allocator(PageManager& page_manager)
            : page_manager_(page_manager)
        {}

        void* allocate() {
            // TODO:
            // Scan for non-full pages first
            // The goal is to allocate from lowest address always.
            // Non-full page might also be completely empty to be returned to OS.
            auto index = non_full_pages_.tzcnt();
            if (index < non_full_pages_.size()) {
                non_full_pages_.clear_bit(index);
                return page_manager_.get_page(index);
            }

            return page_manager_.allocate();
        }

        void deallocate(void* ptr) {

        }

        void mark_non_full(void* ptr) {
            non_full_pages_.set_bit(page_manager_.get_page_index(ptr));
        }

    private:
        PageManager& page_manager_;

        // TODO: need to have a range of pages to scan...
        bitmap<PageManager::PageCount> non_full_pages_;
    };

    template< typename T, typename Allocator > struct pool_allocator {
        static constexpr std::size_t N = 64; //(PageAllocator::PageSize -
            //(PageAllocator::PageSize / sizeof(T) / 8 )) / sizeof(T);

        struct page {
            page(uint64_t init)
                : allocations(init) {}

            uint8_t memory[sizeof(T) * N];
            bitmap<N> allocations;
        };

        static_assert(sizeof(page) <= Allocator::PageSize);

        pool_allocator(Allocator& allocator)
            : pool_page_allocator_(allocator) {}

        ~pool_allocator() {
            page_ = &dummy_page_;
        }

        // allocate has one predictable branch in the fast-path
        T* allocate(std::size_t n) {
            assert(n == 1);
        again:
            auto index = page_->allocations.tzcnt();
            if (__likely__(index < page_->allocations.size())) {
                assert(page_->allocations.get_bit(index) == 1);
                page_->allocations.clear_bit(index);
                return reinterpret_cast<T*>(&page_->memory[sizeof(T) * index]);
            }

            if (page_ != &dummy_page_) {
                // TODO: page is full until deallocate call
                // Do we need this code?
                // pool_page_allocator_.mark_full(page_);
            }

            page* p = page_;
            page_ = (page*)pool_page_allocator_.allocate();
            assert(page_ != p);
            if (!page_) {
                std::abort();
                return nullptr;
            }
            new (page_) page(-1);
goto again;
        }

        // It would be nice to have no branch in deallocation path
        // as it can't fail
        void deallocate(T* ptr) {
            page* p = get_page(ptr);
            assert(p->allocations.get_bit(get_element_index(ptr)) == 0);
            p->allocations.set_bit(get_element_index(ptr));

        #if 0
            // Here we should handle two cases for page_ != p:
            //  1) N - page should be deallocated
            //  2) 1 - page should be set ready for allocation again

            if (__unlikely__(p != page_)) {
                if (p->allocations.get() == -1) {
                    pool_page_allocator_.deallocate(p);
                } else if (p->allocations.get() == 1) {
                    pool_page_allocator_.mark_non_full(p);
                }
            }
        #else
            // Perhaps the above two states can be handled in page_manager::allocate()?
            // Page will be queued for allocations when replaced due to being full.
            // Later, page will be tried for allocating.
            //  If not full, it will be used.
            //  If still full, it will be moved somewhere. Where?
            //      To some list of full pages. The catch is that sometimes,
            //      we need to quickly scan this list to find empty/partially full pages.
            //

            // TODO: this branch is slow...
            // Especially for parallel deallocation this will need some work.
            if (__unlikely__(p != page_)) {
                pool_page_allocator_.mark_non_full(p);
            }
        #endif
        }

        static page* get_page(void* ptr) {
            return reinterpret_cast<page*>
                    (reinterpret_cast<uintptr_t>(ptr) & ~(Allocator::PageSize - 1));
        }

        static uint64_t get_element_index(void* ptr) {
            uint64_t index = (reinterpret_cast<uint64_t>(ptr) - reinterpret_cast<uint64_t>(get_page(ptr))) / sizeof(T);
            assert(index < N);
            return index;
        }

        Allocator& pool_page_allocator_;

        static page dummy_page_;
        static page* page_;
    };

    template<typename T, typename Allocator> typename pool_allocator<T, Allocator>::page pool_allocator<T, Allocator>::dummy_page_(0);
    template<typename T, typename Allocator> typename pool_allocator<T, Allocator>::page* pool_allocator<T, Allocator>::page_ = &pool_allocator<T, Allocator>::dummy_page_;

}


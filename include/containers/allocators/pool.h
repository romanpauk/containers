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
                auto tmp = _tzcnt_u64(values_[i]);
                cnt += tmp;
                if (tmp < sizeof(T) * 8)
                    break;
            }

            return cnt;
        }

        static constexpr std::size_t size() { return N; }

    private:
        std::array<T, N / sizeof(T)> values_;
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

    template<std::size_t Size, std::size_t Count> struct page_manager {
        static constexpr std::size_t PageSize = Size;
        static_assert((PageSize & (PageSize - 1)) == 0);
        static constexpr std::size_t PageCount = Count;

        page_manager() {
            mmap_base_ = (uint64_t)mmap(0, PageSize * PageCount, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            mmap_current_ = mmap_base_;
        }

        ~page_manager() {
            munmap((void*)mmap_base_, PageSize * PageCount);
        }

        void* allocate() {
            auto index = deallocated_pages_.tzcnt();
            if (index < deallocated_pages_.size()) {
                deallocated_pages_.clear_bit(index);
                return get_page(index);
            }

            uint64_t ptr = mmap_current_;
            mmap_current_ += PageSize;
            if (mmap_current_ > mmap_base_ + PageSize * PageCount)
                return nullptr;

            return (void*)ptr;
        }

        void deallocate(void* ptr) {
            deallocated_pages_.set_bit(get_page_index(ptr));
        }

        uint64_t get_page_index(void* ptr) {
            return ((uint64_t)ptr - mmap_base_) / PageSize;
        }

        void* get_page(uint64_t index) {
            return (void*)(mmap_base_ + PageSize * index);
        }

    private:
        uint64_t mmap_base_;
        uint64_t mmap_current_;

        bitmap<PageCount> deallocated_pages_;
    };

    template<typename T, typename PageManager> struct pool_page_allocator {
        static constexpr std::size_t PageSize = PageManager::PageSize;
        static_assert((PageSize & (PageSize - 1)) == 0);
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
            bitmap<64> allocations;
        };

        static_assert(sizeof(page) <= Allocator::PageSize);
        static_assert(bitmap<64>::size() <= N);

        pool_allocator(Allocator& allocator)
            : pool_page_allocator_(allocator) {}

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

            page_ = (page*)pool_page_allocator_.allocate();
            if (!page_) {
                std::abort();
                return nullptr;
            }
            new (page_) page(-1);
goto again;
        }

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
            if (__unlikely__(p->allocations.get() > 0)) {
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


//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <sys/mman.h>
#include <vector>

#include <containers/atomic_bitset.h>
#include <containers/atomic_bitset_heap.h>

#define VBR_PAGE_ALLOCATOR_ATOMIC

#define __likely__(cond) __builtin_expect((cond), true)
#define __unlikely__(cond) __builtin_expect((cond), false)

namespace containers {
    namespace detail {
        inline void* align(void* ptr, std::size_t alignment) {
            assert((alignment & (alignment - 1)) == 0);
            return (void*)(((uintptr_t)ptr + alignment - 1) & ~(alignment - 1));
        }

        template < typename T > T* mask(T* ptr, std::size_t alignment) {
            assert((alignment & (alignment - 1)) == 0);
            return (T*)((uintptr_t)ptr & ~(alignment - 1));
        }

        static constexpr std::size_t round_up(std::size_t v) {
            v--;
            v |= v >> 1;
            v |= v >> 2;
            v |= v >> 4;
            v |= v >> 8;
            v |= v >> 16;
            v++;
            return v;
        }
    }

    enum class PageState {
        Decommitted = 0,
        Active = 1,
        Full = 2,
        Queued = 3,
    };

    // TODO: this will need another page layer to minimize number of mmap calls
    template< typename Page, std::size_t PageCount, std::size_t PageSize > struct vbr_page_allocator {
        static_assert(sizeof(Page) <= PageSize);

        std::atomic<uint64_t> version_ = 0x100;

        void* mmap_ = nullptr;
        static constexpr std::size_t MmapSize = PageCount * PageSize + PageSize - 1;

        uintptr_t pages_begin_ = 0;
        std::atomic<uintptr_t> pages_current_ = 0;

    #if !defined(VBR_PAGE_ALLOCATOR_ATOMIC)
        struct heap {
            void push(uint64_t value) {
                std::lock_guard lock(mutex_);
                heap_.emplace_back(value);
                std::make_heap(heap_.begin(), heap_.end());
            }

            bool pop(uint64_t& value) {
                std::lock_guard lock(mutex_);
                if (heap_.empty())
                    return false;
                std::pop_heap(heap_.begin(), heap_.end());
                value = heap_.back();
                heap_.pop_back();
                return true;
            }

        private:
            std::mutex mutex_;
            std::vector<uint64_t> heap_;
        };

        heap queued_pages_;
        heap decommitted_pages_;
    #else
        atomic_bitset_heap< uintptr_t, detail::round_up(PageCount) > queued_pages_;
        atomic_bitset_heap< uintptr_t, detail::round_up(PageCount) > decommitted_pages_;
    #endif

        vbr_page_allocator() {
            mmap_ = mmap(0, MmapSize, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
            if (mmap_ == MAP_FAILED) // TODO:
                std::abort();
            pages_current_ = pages_begin_ = (uintptr_t)detail::align(mmap_, PageSize);
        }

        ~vbr_page_allocator() {
            munmap(mmap_, MmapSize);
        }

        uint64_t get_version() { return version_.fetch_add(1, std::memory_order_relaxed); }

        Page* allocate_page() {
            auto p = allocate_page_impl();
            assert(p->state() & (int)PageState::Active);
            return p;
        }

        Page* allocate_page_impl() {
            uintptr_t index;
            while (queued_pages_.pop(index)) {
                auto p = get_page_at_index(index);

                // Construct expected state as in case of deallocated page,
                // page will be zeroed, so we can't trust it unless we
                // successfully CAS into it for the first time.
                uint64_t state = (p->state() & ~0xFF) | (int)PageState::Queued;
                if (p->update_state_version(state, get_version() | (int)PageState::Active)) {
                    // Page state was queued (= not deallocated)
                  if (p->refresh_allocations()) {
                    return p;
                  } else {
                    // TODO: can this happen?
                      std::abort();
                  }
               }
            }

            while (decommitted_pages_.pop(index)) {
                auto p = get_page_at_index(index);

                if (mmap(p, PageSize, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0) == MAP_FAILED) {
                    std::abort();
                }

                new (p) Page(get_version() | (int)PageState::Active);
                return p;
            }

            return allocate_new_page();
        }

        Page* allocate_new_page() {
            // TODO: overflow
            uintptr_t address = pages_current_.fetch_add(PageSize);
            if (address + sizeof(Page) > (uintptr_t)mmap_ + MmapSize) {
                // TODO: OOM
                std::abort();
            }
            Page* p = (Page*)address;
            if (mmap(p, PageSize, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0) == MAP_FAILED) {
                std::abort();
            }

            // TODO: version
            new(p) Page(get_version() | (int)PageState::Active);
            return p;
        }

        void queue_page(Page* p) {
            assert(p->state() & (int)PageState::Queued);

            // TODO: we can queue a page that was deallocated. Should not matter
            // as it is handled in allocate_page().
            queued_pages_.push(get_page_index(p));
        }

        void decommit_page(Page* p) {
            //assert(p->state() & Page::decommitted);

            // TODO: we can deallocate page that could still be queued later.
            // Should not matter as it is handled in allocate_page().
            //
            // How to do the deallocation?
            // Write to deallocated queue and deallocate from the back.

            if (mmap(p, PageSize, PROT_READ, MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0) == MAP_FAILED) {
                std::abort();
            }

            assert(p->state() == 0);

            decommitted_pages_.push(get_page_index(p));
        }

        uintptr_t get_page_index(Page* p) {
            assert((uintptr_t)p >= pages_begin_);
            assert((uintptr_t)p + sizeof(Page) <= (uintptr_t)mmap_ + MmapSize);
            return ((uintptr_t)p - pages_begin_) / PageSize;
        }

        Page* get_page_at_index(uintptr_t index) {
            assert(index <= PageCount);
            return (Page*)(pages_begin_ + index * PageSize);
        }
    };

    template< typename T > struct alignas(16) vbr_ptr {
        vbr_ptr() = default;
        vbr_ptr(std::nullptr_t) {}
        vbr_ptr(uint64_t v, uint64_t p): version(v), ptr(p) {}

        mutable uint64_t version = 0;
        mutable uint64_t ptr = 0;

        operator bool() const {
            return ptr != 0;
        }

        bool operator == (vbr_ptr<T> other) {
            return version == other.version &&
                ptr == other.ptr;
        }

        bool operator == (std::nullptr_t) {
            return ptr == 0;
        }
    };

    template< typename T, std::size_t N = 1ull<<32, std::size_t PageSize = 4096 > struct vbr_allocator {
        static constexpr std::size_t PageElementCount =
            (PageSize -
                sizeof(uint8_t) -
                sizeof(uint8_t) -
                sizeof(uint8_t) -
                sizeof(atomic_bitset<256>) -
                sizeof(uint64_t) -
                128)
                / (sizeof(uint8_t) + sizeof(T));

        static_assert(PageElementCount < 256);

        struct page {
            // Local, for owning thread only
            uint8_t allocations_size_;
            uint8_t allocations_[PageElementCount];

            // Shared between threads
            alignas(64) std::atomic<uint8_t> deallocations_size_;
            atomic_bitset<256> deallocations_;

            alignas(64) std::atomic<uint64_t> state_;
            alignas(64) T values_[PageElementCount];

            static constexpr uint8_t capacity() { return PageElementCount; }

            page(uint64_t state) {
                for (std::size_t i = 0; i < PageElementCount; ++i)
                    allocations_[i] = PageElementCount - i - 1;
                allocations_size_ = PageElementCount;
                deallocations_.clear();
                deallocations_size_.store(0, std::memory_order_relaxed);
                state_.store(state, std::memory_order_relaxed);
            }

            uint8_t allocations_size() const { return allocations_size_; }

            T* allocate() {
                assert(allocations_size_ > 0 && allocations_size_ <= PageElementCount);
                return &values_[allocations_[--allocations_size_]];
            }

            void deallocate(T* ptr) {
                assert(ptr >= values_ && ptr < values_ + PageElementCount);
                deallocations_.set(ptr - values_);
                deallocations_size_.fetch_add(1, std::memory_order_relaxed);
            }

            uint8_t deallocations_size() const {
                return deallocations_size_.load(std::memory_order_acquire);
            }

            uint64_t state() const { return state_.load(std::memory_order_relaxed); }
            uint64_t version() const { return state() & ~0xFF; }

            bool update_state(uint64_t& state, PageState value) {
                return state_.compare_exchange_strong(state, (state & ~0xFF) | (int)value, std::memory_order_relaxed);
            }

            bool update_state_version(uint64_t& state, uint64_t value) {
                return state_.compare_exchange_strong(state, value, std::memory_order_relaxed);
            }

            bool refresh_allocations() {
                // acquire?
                std::size_t deallocations = 0;
                for(std::size_t i = 0; i < deallocations_.word_size(); ++i) {
                    // TODO: word is a small bitset, need next_bit().
                    auto word = deallocations_.exchange_word(i, 0);
                    auto offset = sizeof(word) * 8 * i;
                    for (std::size_t j = 0; j < sizeof(word) * 8; ++j) {
                        if (word & (1ull << j)) {
                            assert(allocations_size_ < PageElementCount);
                            allocations_[allocations_size_++] = offset + j;
                            deallocations += 1;
                        }
                    }
                }
                deallocations_size_.fetch_sub(deallocations, std::memory_order_relaxed);
                return deallocations > 0;
            }
        };

        static_assert(sizeof(page) <= PageSize);

        vbr_page_allocator< page, (N + PageElementCount) / PageElementCount, PageSize > page_allocator_;

        page* page_ = nullptr;

        vbr_allocator() {
            page_ = page_allocator_.allocate_page();
        }

        T* allocate_impl() {
            // unlikely
            if (__unlikely__(page_->allocations_size() == 1)) {

                // TODO: handle special case here when the page_ is dummy,
                // so we don't need to allocate in constructor.
                uint64_t state = page_->state();
                //if (state == PageState::Dummy) {
                //    goto allocate_page;
                //}

                T* ptr = page_->allocate();
                assert(state & (int)PageState::Active);
                if (page_->update_state(state, PageState::Full)) {
                    state &= ~0xFF;
                    state |= (int)PageState::Full;
                    if (page_->refresh_allocations()) {
                        if (page_->update_state(state, PageState::Active))
                            return ptr;
                    }
                }

            //allocate_page:
                page_ = page_allocator_.allocate_page();
                return ptr;
            }

            return page_->allocate();
        }

        page* get_page(T* ptr) {
            return (page*)detail::mask(ptr, PageSize);
        }

        uint64_t get_version(T* ptr) {
            return get_page(ptr)->state() & ~0xFF;
        }

        void deallocate(T* ptr) {
            auto p = get_page(ptr);
            //
            // TODO: we can avoid this load, if each deallocate() call returns if this was
            // the first deallocation or not. If it was first, we will try to do the load
            // and eventually move the state from full to queued.
            // The same logic could apply to full deallocation. If the word is fully deallocated,
            // we can check deallocated size and eventually deallocate...
            //
            uint64_t state = p->state();
            p->deallocate(ptr);
            if (state & (int)PageState::Active)
                return;

            assert((state & (int)PageState::Full) || (state & (int)PageState::Queued));

            if (!(state & (int)PageState::Queued)) {
                if(p->update_state(state, PageState::Queued)) {
                    page_allocator_.queue_page(p);
                }
            }

            if (p->capacity() == p->deallocations_size()) {
                if (p->update_state(state, PageState::Decommitted)) {
                    page_allocator_.decommit_page(p);
                }
            }
        }

        vbr_ptr<T> allocate() {
            T* ptr = allocate_impl();
            return {get_version(ptr), (uint64_t)ptr};
        }

        template< typename... Args > void construct(vbr_ptr<T> p, Args&&... args) {
            new(read(p)) T { std::forward<Args>(args)... };
        }

        T* read(vbr_ptr<T> p) {
            return (T*)p.ptr;
        }

        void destroy(vbr_ptr<T> p) {
            if (!std::is_trivially_destructible_v<T>)
                read(p)->~T();
        }

        void deallocate(vbr_ptr<T> p) {
            deallocate((T*)p.ptr);
        }
    };
}

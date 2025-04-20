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
    }

    // TODO: this will need another page layer to minimize number of mmap calls
    template< typename Page, std::size_t PageCount, std::size_t PageSize > struct vbr_page_allocator {
        static_assert(sizeof(Page) <= PageSize);

        std::atomic<uint64_t> version_ = 0x100;

        void* mmap_ = nullptr;
        static constexpr std::size_t MmapSize = PageCount * PageSize + PageSize - 1;

        std::atomic<uintptr_t> memory_ = 0;

        std::mutex mutex_;
        std::vector<Page*> queued_pages_;
        std::vector<Page*> decommitted_pages_;

        vbr_page_allocator() {
            mmap_ = mmap(0, MmapSize, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
            if (mmap_ == MAP_FAILED) // TODO:
                std::abort();
            memory_ = (uintptr_t)detail::align(mmap_, PageSize);
        }

        ~vbr_page_allocator() {
            munmap(mmap_, MmapSize);
        }

        uint64_t get_version() { return version_.fetch_add(1, std::memory_order_relaxed); }

        Page* allocate_page() {
            std::lock_guard lock(mutex_);

            while (!queued_pages_.empty()) {
                std::pop_heap(queued_pages_.begin(), queued_pages_.end());
                auto p = queued_pages_.back();
                queued_pages_.pop_back();

                // Construct expected state as in case of deallocated page,
                // page will be zeroed, so we can't trust it unless we
                // successfully CAS into it for the first time.
                uint64_t state = (p->state() & ~0xFF) | Page::queued;
                if (p->update_state_version(state, get_version() | Page::active)) {
                    // Page state was queued (= not deallocated)
                  if (p->refresh_allocations()) {
                    return p;
                  } else {
                    // TODO: can this happen?
                  }
               }
            }

            while (!decommitted_pages_.empty()) {
                std::pop_heap(decommitted_pages_.begin(), decommitted_pages_.end());
                auto p = decommitted_pages_.back();
                decommitted_pages_.pop_back();

                if (mmap(p, PageSize, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0) == MAP_FAILED) {
                    std::abort();
                }

                // TODO: update version
                new (p) Page();
                return p;
            }

            return allocate_new_page();
        }

        Page* allocate_new_page() {
            // TODO: overflow
            uintptr_t address = memory_.fetch_add(PageSize);
            if (address + sizeof(Page) > (uintptr_t)mmap_ + MmapSize) {
                // TODO: OOM
                std::abort();
            }
            Page* p = (Page*)address;
            if (mmap(p, PageSize, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0) == MAP_FAILED) {
                std::abort();
            }

            // TODO: version
            new(p) Page();
            assert(p->state() == Page::active);
            return p;
        }

        void queue_page(Page* p) {
            // TODO: we can queue a page that was deallocated. Should not matter
            // as it is handled in allocate_page().

            std::lock_guard lock(mutex_);
            queued_pages_.emplace_back(p);
            std::make_heap(queued_pages_.begin(), queued_pages_.end());
        }

        void decommit_page(Page* p) {
            // TODO: we can deallocate page that could still be queued later.
            // Should not matter as it is handled in allocate_page().
            //
            // How to do the deallocation?
            // Write to deallocated queue and deallocate from the back.

            if (mmap(p, PageSize, PROT_READ, MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0) == MAP_FAILED) {
                std::abort();
            }

            assert(p->state() == Page::decommitted);

            std::lock_guard lock(mutex_);
            decommitted_pages_.emplace_back(p);
            std::make_heap(decommitted_pages_.begin(), decommitted_pages_.end());
        }
    };

    template< typename T, std::size_t N = 1<<30, std::size_t PageSize = 4096 > struct vbr_allocator {
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
            enum state {
                decommitted = 0,
                active = 1,
                full = 2,
                queued = 3,
            };

            // Local, for owning thread only
            uint8_t allocations_size_;
            uint8_t allocations_[PageElementCount];

            // Shared between threads
            alignas(64) std::atomic<uint8_t> deallocations_size_;
            atomic_bitset<256> deallocations_;

            alignas(64) std::atomic<uint64_t> state_;
            alignas(64) T values_[PageElementCount];

            static constexpr uint8_t capacity() { return PageElementCount; }

            page() {
                for (std::size_t i = 0; i < PageElementCount; ++i)
                    allocations_[i] = PageElementCount - i - 1;
                allocations_size_ = PageElementCount;
                deallocations_.clear();
                deallocations_size_.store(0, std::memory_order_relaxed);
                state_.store(active, std::memory_order_relaxed);
            }

            uint8_t allocations_size() const { return allocations_size_; }

            T* allocate() {
                assert(allocations_size_ > 0 && allocations_size_ <= PageElementCount);
                return &values_[allocations_[--allocations_size_]];
            }

            void deallocate(T* ptr) {
                assert(ptr >= values_ && ptr < values_ + PageElementCount);
                deallocations_.set(ptr - values_);
                deallocations_size_.fetch_add(1, std::memory_order_release);
            }

            uint8_t deallocations_size() const {
                return deallocations_size_.load(std::memory_order_acquire);
            }

            uint64_t state() const { return state_.load(std::memory_order_relaxed); }

            bool update_state(uint64_t& state, uint8_t value) {
                return state_.compare_exchange_strong(state, (state & ~0xFF) | value);
            }

            bool update_state_version(uint64_t& state, uint64_t value) {
                return state_.compare_exchange_strong(state, value);
            }

            bool refresh_allocations() {
                // acquire?
                std::size_t deallocations = 0;
                for(std::size_t i = 0; i < deallocations_.word_size(); ++i) {
                    // TODO: word is a small bitset, need next_bit().
                    auto word = deallocations_.exchange_word(i, 0);
                    auto offset = sizeof(word) * 8 * i;
                    for (std::size_t j = 0; j < sizeof(word) * 8; ++j) {
                        if (word & (1 << j)) {
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

        vbr_page_allocator< page, N / PageElementCount, PageSize > page_allocator_;

        page* page_ = nullptr;

        vbr_allocator() {
            page_ = page_allocator_.allocate_page();
        }

        T* allocate() {
            // unlikely
            if (page_->allocations_size() == 1) {

                // TODO: handle special case here when the page_ is dummy,
                // so we don't need to allocate in constructor.

                T* ptr = page_->allocate();

                uint64_t state = page_->state();
                assert(state & page::active);
                if (page_->update_state(state, page::full)) {
                    state |= page::full;
                    if (page_->refresh_allocations()) {
                        if (page_->update_state(state, page::active))
                            return ptr;
                    }
                }

                page_ = page_allocator_.allocate_page();
                return ptr;
            }

            return page_->allocate();
        }

        page* get_page(T* ptr) {
            return (page*)detail::mask(ptr, PageSize);
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
            if (state & page::active)
                return;

            assert((state & page::full) || (state & page::queued));

            if (!(state & page::queued)) {
                if(p->update_state(state, page::queued)) {
                    page_allocator_.queue_page(p);
                }
            }

            if (p->capacity() == p->deallocations_size()) {
                if (p->update_state(state, page::decommitted)) {
                    page_allocator_.decommit_page(p);
                }
            }
        }
    };
}

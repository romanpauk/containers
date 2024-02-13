//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <array>
#include <cassert>
#include <vector>
#include <cstdint>
#include <algorithm>

#if defined(_MSV_VER)
#include <intrin.h>
#endif

#define MEMORY

namespace containers {
    template< typename T > struct MurmurMix {
        size_t operator()(T value) const {
            size_t h = value;
            h ^= h >> 33;
            h *= 0xff51afd7ed558ccdL;
            h ^= h >> 33;
            h *= 0xc4ceb9fe1a85ec53L;
            h ^= h >> 33;
            return h;
        }
    };

    template< typename T > struct H
    {
        size_t operator()(T value) const {
            size_t h = value;
            h *= 0xc4ceb9fe1a85ec53L;
            //h ^= h >> 33;
            return h;
        }
    };

    template< typename T, size_t N > struct fixed_hash_table1 {
        size_t size_{};
        size_t collisions_{};
        std::array< T, N > values_{};

        bool insert(T key, size_t hash) {
            for (size_t i = 0; i < values_.size(); ++i) {
                size_t index = (hash + i) & (values_.size() - 1);
                if (values_[index] == 0) {
                    values_[index] = key;
                    ++size_;
                    return true;
                }
                else if (values_[index] == key) {
                    return true;
                }
                else {
                    ++collisions_;
                }
            }

            return false;
        }

        size_t get_index(T key, size_t hash) {
            for (size_t i = 0; i < values_.size(); ++i) {
                size_t index = (hash + i) & (values_.size() - 1);
                if (values_[index] == key) {
                    return index;
                }
            }
            return values_.size();
        }

        size_t size() const { return size_; }
        size_t collisions() const { return collisions_; }

        auto begin() { return values_.begin(); }
        auto end() { return values_.end(); }
    };

    template< typename T, size_t N > struct fixed_hash_table2
    {
        static_assert(N >= 256);
        size_t size_{};

        size_t fastpath_{};
        size_t fastpath_collisions_{};
        size_t slowpath_{};
        size_t slowpath_collisions_{};

        std::array< T, N > values_{};

        bool insert(T key, size_t hash) {
            const uint8_t* hashp = (uint8_t*)&hash;
            for (const uint8_t* p = hashp; p < hashp + 8; ++p) {
                if (values_[*p] == 0) {
                    values_[*p] = key;
                    ++size_;
                    fastpath_++;
                    return true;
                }
                else if (values_[*p] == key) {
                    fastpath_++;
                    return true;
                }
                else {
                    ++fastpath_collisions_;
                }
            }

            for (size_t i = 0; i < values_.size(); ++i) {
                size_t index = (hash + i) & (values_.size() - 1);
                if (values_[index] == 0) {
                    values_[index] = key;
                    ++size_;
                    slowpath_++;
                    return true;
                }
                else if (values_[index] == key) {
                    slowpath_++;
                    return true;
                }
                else {
                    ++slowpath_collisions_;
                }
            }

            return false;
        }

        size_t get_index(T key, size_t hash) {
            const uint8_t* hashp = (uint8_t*)&hash;
            for (const uint8_t* p = hashp; p < hashp + 8; ++p) {
                if (values_[*p] == key) {
                    return *p;
                }
            }

            for (size_t i = 0; i < values_.size(); ++i) {
                size_t index = (hash + i) & (values_.size() - 1);
                if (values_[index] == key) {
                    return index;
                }
            }

            return values_.size();
        }

        size_t size() const { return size_; }

        auto begin() { return values_.begin(); }
        auto end() { return values_.end(); }
    };

    template< typename T, size_t N > struct metadata {
        std::array<T, N> array_{};

        size_t find(T fp) {
            for (size_t i = 0; i < array_.size(); ++i) {
                if (array_[i] == fp)
                    return i;
            }
            return N;
        }

        void insert(size_t index, T fp) { 
            assert(array_[index] == 0);
            array_[index] = fp;
        }

        static constexpr size_t size() { return N; }
    };

    template< typename T, size_t N > struct metadata2 {
        alignas(32) std::array<T, N> array_{};

        size_t find(T fp)
        {
            for (size_t i = 0; i < array_.size(); ++i) {
                if (array_[i] == fp)
                    return i;
            }
            return N;
        }

        void insert(size_t index, T fp)
        {
            assert(array_[index] == 0);
            array_[index] = fp;
        }

        static constexpr size_t size() { return N; }
    };

    template< typename T, size_t N > struct fixed_hash_table3 {
        size_t size_{};
        metadata2<uint8_t, N> meta_{};
        std::array< T, N > values_{};

        bool insert(T key, size_t hash) {
            //++size_;
            //return true;
            const uint8_t* hashp = (uint8_t*)&hash;
            uint8_t index = meta_.find(*hashp);
            if (index != meta_.size()) {
                return values_[index] == key;
            } else {
                index = meta_.find(0);
                if (index < meta_.size()) {
                    meta_.insert(index, *hashp);
                    values_[index] = key;
                    return true;
                }
            }

            return false;
        }

        size_t get_index(T key, size_t hash) {
            const uint8_t* hashp = (uint8_t*)&hash;
            uint8_t index = meta_.find(*hashp);
            if (index != meta_.size()) {
                if (values_[index] == key)
                    return index;
            }

            return N;
        }

        size_t size() const { return size_; }

        auto begin() { return values_.begin(); }
        auto end() { return values_.end(); }
    };

    template< typename Key, typename Hash = H<Key>, size_t PageSize = 256 > class eh_hash_table {
        struct page {
            size_t depth_ = 0;
        #if defined(MEMORY)
            size_t refs_ = 0;
        #endif
            fixed_hash_table2<Key, PageSize> values_;
        };

        size_t depth_ = 0;
        std::vector< page* > pages_;

        size_t pageindex(size_t hash) {
            return hash & ((1ull << depth_) - 1);
        }

        size_t keyindex(size_t hash) {
        #if defined(_MSC_VER)
            return _byteswap_uint64(hash);
        #elif defined(__GNUC__)
            return __builtin_bswap64(hash);
        #else
        #error "not supported"
        #endif
        }

    public:
        eh_hash_table() {
        #if defined(MEMORY)
            pages_.reserve(1024);
        #endif
            pages_.push_back(new page());
        #if defined(MEMORY)
            ++pages_[0]->refs_;
        #endif
        }

        ~eh_hash_table() {
        #if defined(MEMORY)
            for (auto* p : pages_) {
                if(--p->refs_ == 0)
                    delete p;
            }
        #endif
        }

        void insert(Key key) {
            size_t kh = Hash()(key);
            page* p = pages_[pageindex(kh)];
            if (p->values_.size() >= PageSize * 3/4) {
                if (p->depth_ == depth_) {
                    pages_.resize(pages_.size() * 2);
                    for (size_t i = 0; i < pages_.size() / 2; ++i) {
                    #if defined(MEMORY)
                        ++pages_[i]->refs_;
                    #endif
                        pages_[i + pages_.size() / 2] = pages_[i];
                    }
                    ++depth_;
                }
                page* p0 = new page();
                page* p1 = new page();
                p0->depth_ = p1->depth_ = p->depth_ + 1;
                size_t high_bit = 1 << p->depth_;
                for (auto& v : p->values_) {
                    if (v == 0)
                        continue;
                    auto vh = Hash()(v);
                    auto* n = (pageindex(vh) & high_bit) ? p1 : p0;
                    n->values_.insert(v, keyindex(vh));
                }
                
                for (size_t i = kh & (high_bit - 1); i < pages_.size(); i += high_bit) {
                #if defined(MEMORY)
                    --pages_[i]->refs_;
                #endif
                    pages_[i] = (i & high_bit) ? p1 : p0;
                #if defined(MEMORY)
                    ++pages_[i]->refs_;
                #endif
                }

                delete p;
            } else {
                p->values_.insert(key, keyindex(kh));
            }
            
        }

        size_t get(Key key) {
            size_t kh = Hash()(key);
            page* p = pages_[pageindex(kh)];
            return p->values_.get_index(key, keyindex(kh));
        }

        double occupancy() {
            size_t used = 0;
            size_t available = PageSize * pages_.size();
            for (page* p : pages_) {
                used += p->values_.size();
            }
            return double(used)/available;
        }

        size_t fast()
        {
            size_t cnt = 0;
            for (page* p : pages_) {
                //cnt += p->values_.fastpath_;
            }
            return cnt;
        }

        size_t fast_collisions()
        {
            size_t cnt = 0;
            for (page* p : pages_) {
                //cnt += p->values_.fastpath_collisions_;
            }
            return cnt;
        }

        size_t slow()
        {
            size_t cnt = 0;
            for (page* p : pages_) {
                //cnt += p->values_.slowpath_;
            }
            return cnt;
        }

        size_t slow_collisions()
        {
            size_t cnt = 0;
            for (page* p : pages_) {
                //cnt += p->values_.slowpath_collisions_;
            }
            return cnt;
        }
    };

    template< typename Key, typename Hash = H<Key>, size_t BucketSize = 64 > class flat_hash_table {
        struct Bucket {
            //uint8_t meta[BucketSize];
            alignas(Key) uint8_t storage[BucketSize * sizeof(Key)];
        };

        size_t buckets_size_;
        //Bucket* buckets_[];

        Key* values_;
        size_t size_ = 0;
        size_t capacity_ = 0;
    public:
        flat_hash_table()
            : values_() {
            values_ = (Key*)::operator new(sizeof(Key) * 64);
            capacity_ = 64;
            std::memset(values_, 0, capacity_ * sizeof(Key));
        }

        ~flat_hash_table() { ::operator delete(values_, capacity_); }

        bool insert(Key key) {
            if (size_ * 4 == capacity_ * 3) {
                auto p = values_;
                values_ = (Key*)::operator new(sizeof(Key)*capacity_ * 2);

                //for (Key* b = values_, *e = values_ + capacity_ * 2; b != e; ++b) {
                //    new(b) Key();
                //}
                std::memcpy(values_, p, sizeof(Key) * capacity_);
                std::memset(values_ + capacity_, 0, sizeof(Key) * capacity_);
                //memset(values_, 0, capacity_ * 2 * sizeof(Key));
                //for (auto* px = p; px != p + capacity_; ++px) {
                    //if (*px)
                    //    insert_impl(*px);
                    
                //}

                ::operator delete(p, capacity_);
                capacity_ *= 2;

                //values_.resize(values_.size() * 2);
                /*
                for (auto& v : values_) {
                    if (v == 0)
                        continue;
                    auto vc = v;
                    //v = 0;
                    //insert_impl(vc);
                }*/
            }

            size_++;
            return true;
            //return insert_impl(key);
        }

        bool insert_impl(Key key) {
            auto h = Hash()(key);
            //size_t index = (h) & (values_.size() - 1);
            //if (values_[index] == 0) {
            //    values_[index] = key;
            //    ++size_;
            //    return true;
            //}
            //return false;
            size_t count = capacity_; //std::min(values_.size() / 2, 2ull);
            for (size_t i = 0; i < 1; ++i) {
                size_t index = (h + i) & (capacity_ - 1);
                if (values_[index] == 0) {
                    values_[index] = key;
                    size_++;
                    return true;
                }
            }
            return false;
        }
    };
}

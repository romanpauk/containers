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
            return h;
        }
    };

    template< typename Key, typename Hash = H<Key>, size_t PageSize = 128 > class hash_table {
        struct page {
            size_t depth_{};
            size_t size_{};
            size_t collisions_{};
            std::array< Key, PageSize > values_{};

            bool insert(Key key, size_t hash) {
                for (size_t i = 0; i < values_.size(); ++i) {
                    size_t index = (hash + i) & (values_.size() - 1);
                    if (values_[index] == 0) {
                        values_[index] = key;
                        ++size_;
                        return true;
                    } else if(values_[index] == key) {
                        return true;
                    } else {
                        ++collisions_;
                    }
                }

                return false;
            }

            size_t get_index(Key key, size_t hash) {
                for (size_t i = 0; i < values_.size(); ++i) {
                    size_t index = (hash + i) & (values_.size() - 1);
                    if (values_[index] == key) {
                        return index;
                    }
                }
                return values_.size();
            }
        };

        size_t depth_ = 0;
        std::vector< page* > pages_;

        size_t pageindex(size_t hash) {
            return hash & ((1 << depth_) - 1);
        }

        size_t keyindex(Key key, size_t hash) {
            return hash ^ (hash >> 31);
            return key;
        }

    public:
        hash_table()
        {
            //pages_.reserve(1024);
            pages_.push_back(new page());
        }

        void insert(Key key) {
            size_t h = Hash()(key);
            //if (depth_ == 0) {
            //    if (pages_[0]->insert(key, keyindex(h)))
            //        return;
            //}
            page* p = pages_[pageindex(h)];
            if(p->insert(key, keyindex(key, h)))
                return;

            if (p->size_ == p->values_.size()) {
                if (p->depth_ == depth_) {
                    pages_.resize(pages_.size() * 2);
                    for (size_t i = 0; i < pages_.size() / 2; ++i)
                        pages_[i + pages_.size() / 2] = pages_[i];
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
                    n->insert(v, keyindex(v, vh));
                }
                
                for (size_t i = h & (high_bit - 1); i < pages_.size(); i += high_bit) {
                    pages_[i] = (i & high_bit) ? p1 : p0;
                }

                delete p;

                //for (size_t i = 0; i < pages_.size(); ++i)
                  //  assert(pages_[i] != p);
            } else
                ; //p->insert(key, keyhash(key));
        }

        size_t get(Key key) {
            size_t h = Hash()(key);
            page* p = pages_[pageindex(h)];
            return p->get_index(key, keyindex(key, h));
        }

        size_t collisions() {
            size_t result = 0;
            for (page* p : pages_) {
                result += p->collisions_;
            }
            return result;
        }
    };
}

//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <optional>
#include <unordered_set>

namespace containers {
    template< typename Key, typename Value > class lru_unordered_map {
        struct node {
            bool operator == (const node& n) const noexcept { return pair.first == n.pair.first; }

            std::pair<const Key, Value> pair;
            mutable const node* next;
            mutable const node* prev;
        };

        struct hash {
            size_t operator()(const node& n) const noexcept { return std::hash<Key>()(n.pair.first); }
        };

        struct node_list {
            const node* head_ = nullptr;
            const node* tail_ = nullptr;

            void push_front(const node* n) {
                if (head_) {
                    n->next = head_;
                    head_->prev = n;
                    head_ = n;
                } else {
                    head_ = tail_ = n;
                }
            }

            void erase(const node* n) {
                if (n->next)
                    n->next->prev = n->prev;
                if (n->prev)
                    n->prev->next = n->next;
            }

            const node* pop_back() {
                const node* n = tail_;
                if (tail_) {
                    tail_ = tail_->prev;
                    if (tail_) {
                        tail_->next = nullptr;
                    } else {
                        head_ = nullptr;
                    }
                }
                return n;
            }

            void clear() { head_ = tail_ = nullptr; }
        };

        using values_type = std::unordered_set< node, hash >;

        values_type values_;
        node_list list_;

    public:
        struct iterator {
            iterator(typename values_type::iterator it): it_(it) {}

            const std::pair<const Key, Value>& operator*() { return it_->pair; }
            const std::pair<const Key, Value>* operator->() { return &it_->pair; }

            bool operator == (const iterator& other) { return it_ == other.it_; }
            bool operator != (const iterator& other) { return it_ != other.it_; }

            iterator& operator++() { return ++it_; }
            iterator operator++(int) { typename values_type::iterator it = it_; ++it_; return it; }
        private:
            typename values_type::iterator it_;
        };

        iterator begin() { return values_.begin(); }
        iterator end() { return values_.end(); }

        template<typename... Args> std::pair<iterator, bool> emplace(Args&&... args) {
            auto it = values_.emplace(node{{std::forward<Args>(args)...}});
            const node* n = &*it.first;
            if (!it.second) {
                list_.erase(n);
            }
            list_.push_front(n);
            return {it.first, it.second};
        }

        iterator find(const Key& key) {
            // TODO: this is solved by heterogenous hashing in C++20, what about C++17?
            return values_.find({{key, Value()}});
        }

        void clear() {
            values_.clear();
            list_.clear();
        }

        size_t size() const { return values_.size(); }
        bool empty() const { return values_.empty(); }

        std::optional<std::pair<const Key, Value>> evict() {
            std::optional<std::pair<const Key, Value>> result;
            auto* n = list_.pop_back();
            if (n) {
                result.emplace(std::move(n->pair));
                values_.erase(*n);                
            }
            return result;
        }
    };
}

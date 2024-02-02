//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <cassert>
#include <optional>
#include <unordered_set>

namespace containers {
    template<
        typename Key,
        typename Value,
        typename Hash = std::hash<Key>,
        typename KeyEqual = std::equal_to<Key>,
        typename Allocator = std::allocator< std::pair<const Key, Value > >
    > class lru_unordered_map
    {
        struct node {
            bool operator == (const node& n) const noexcept { return value.first == n.value.first; }
            bool operator != (const node& n) const noexcept { return value.first != n.value.first; }

            std::pair<const Key, Value> value;
            mutable const node* next;
            mutable const node* prev;
        };

        struct hash: Hash {
            size_t operator()(const node& n) const noexcept { return static_cast<const Hash&>(*this)(n.value.first); }
        };

        struct list {
            struct iterator {
                iterator(const node* n) : node_(n) {}

                const std::pair<const Key, Value>& operator*() { assert(node_); return node_->value; }
                const std::pair<const Key, Value>* operator->() { assert(node_); return &node_->value; }

                bool operator == (const iterator& other) const { return node_ == other.node_; }
                bool operator != (const iterator& other) const { return node_ != other.node_; }

                iterator& operator++() { assert(node_); node_ = node_->next; return *this; }
                iterator operator++(int) { assert(node_); const node* n = node_; node_ = node_->next; return n; }

            private:
                template< typename Key, typename Value, typename Hash, typename KeyEqual, typename Allocator > friend class lru_unordered_map;
                typename const node* node_;
            };

            iterator begin() const {
                assert(!head_ || !head_->prev);
                return head_;
            }

            iterator end() const {
                assert(!tail_ || !tail_->next);
                return nullptr;
            }

        private:
            template< typename Key, typename Value, typename Hash, typename KeyEqual, typename Allocator > friend class lru_unordered_map;

/*
            void push_front(const node& n) {
                if (head_) {
                    assert(!head_->prev);
                    assert(tail_);
                    assert(!tail_->next);
                    n.prev = nullptr;
                    n.next = head_;
                    head_->prev = &n;
                    head_ = &n;
                } else {
                    assert(!tail_);
                    head_ = tail_ = &n;
                    n.prev = n.next = nullptr;
                }
            }
*/
            void push_back(const node& n) {
                if (!tail_) {
                    assert(!head_);
                    tail_ = head_ = &n;
                } else {
                    n.next = nullptr;
                    n.prev = tail_;
                    tail_->next = &n;
                    tail_ = &n;
                }
            }
            
            const node* erase(const node& n) {
                if (n.next) {
                    n.next->prev = n.prev;
                } else {
                    assert(tail_ == &n);
                    tail_ = n.prev;
                }

                if (n.prev) {
                    n.prev->next = n.next;
                } else {
                    assert(head_ == &n);
                    head_ = n.next;;
                }

                return n.next;
            }

            const node& front() const {
                assert(head_);
                return *head_;
            }

            const node& back() const {
                assert(tail_);
                return *tail_;
            }

            void clear() { head_ = tail_ = nullptr; }

            const node* head_ = nullptr;
            const node* tail_ = nullptr;
        };

        using values_type = std::unordered_set< node, hash >;

        values_type values_;
        list list_;

    public:
        struct iterator {
            iterator(typename values_type::iterator it): it_(it) {}

            const std::pair<const Key, Value>& operator*() { return it_->value; }
            const std::pair<const Key, Value>* operator->() { return &it_->value; }

            bool operator == (const iterator& other) const { return it_ == other.it_; }
            bool operator != (const iterator& other) const { return it_ != other.it_; }

            iterator& operator++() { return ++it_; }
            iterator operator++(int) { typename values_type::iterator it = it_; ++it_; return it; }

        private:
            template< typename Key, typename Value, typename Hash, typename KeyEqual, typename Allocator > friend class lru_unordered_map;
            typename values_type::iterator it_;
        };

        iterator begin() { return values_.begin(); }
        iterator end() { return values_.end(); }

        template<typename... Args> std::pair<iterator, bool> emplace(Args&&... args) {
            auto it = values_.emplace(node{{std::forward<Args>(args)...}});
            const node& n = *it.first;
            if (it.second) {
                list_.push_back(n);
            } else if (n != list_.back()) {
                list_.erase(n);
                list_.push_back(n);
            }
            return {it.first, it.second};
        }

        iterator find(const Key& key) {
            // TODO: this is solved by heterogenous hashing in C++20, what about C++17?
            return values_.find({ {key, Value()} });
        }

        Value& operator[](const Key& key) {
            return const_cast<Value&>(emplace(key, Value()).first->second);
        }

        void erase(const Key& key) {
            auto it = find(key);
            if (it != values_.end()) {
                list_.erase(it->second);
                values_.erase(it);
            }
        }

        iterator erase(const iterator& it) {
            list_.erase(*it.it_);
            return values_.erase(it.it_);
        }

        void clear() {
            values_.clear();
            list_.clear();
        }

        size_t size() const { return values_.size(); }
        bool empty() const { return values_.empty(); }

        void touch(const iterator& it) {
            assert(it != end());
            auto& n = *it.it_;
            list_.erase(n);
            list_.push_back(n);
        }

        void touch(const Key& key) {
            auto it = find(key);
            if (it != end()) touch(it);
        }

        const list& evictables() { return list_; }
        
        typename list::iterator erase(const typename list::iterator& it) {
            assert(it != list_.end());
            const node* n = list_.erase(*it.node_);
            values_.erase(*it.node_);
            return n;
        }
    };
}

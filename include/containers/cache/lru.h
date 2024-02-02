//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <optional>
#include <unordered_map>

namespace containers {
    template< typename Key, typename Value > class lru_unordered_map {
        struct node {
            Value value;
            const Key* key;
            node* next;
            node* prev;
        };

        struct node_list {
            node* head_ = nullptr;
            node* tail_ = nullptr;

            void push_front(node* n) {
                if (head_) {
                    n->next = head_;
                    head_->prev = n;
                    head_ = n;
                } else {
                    head_ = tail_ = n;
                }
            }

            void erase(node* n) {
                if (n->next)
                    n->next->prev = n->prev;
                if (n->prev)
                    n->prev->next = n->next;
            }

            node* pop_back() {
                node* n = tail_;
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

        using values_type = std::unordered_map< Key, node >;

        values_type values_;
        node_list list_;

    public:
        struct iterator {
            iterator(typename values_type::iterator it): it_(it) {}

            std::pair<const Key&, Value&> operator*() {
                //return *reinterpret_cast<std::pair<const Key, Value>*>(&*it_);
                return {it_->first, it_->second.value};
            }

        private:
            typename values_type::iterator it_;
        };

        iterator begin() { return values_.begin(); }
        iterator end() { return values_.end(); }

        void emplace(Key k, Value v) {
            auto it = values_.emplace(k, node{v});
            auto* n = &it.first->second;
            if (it.second) {
                n->key = &it.first->first;
            } else {
                list_.erase(n);
            }
            list_.push_front(n);
        }

        Value& get(Key k) {
            auto it = values_.find(k);
            if (it != values_.end()) {
                auto* n = &it->second;
                list_.erase(n);
                list_.push_front(n);
            }
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
                auto it = values_.find(*n->key);
                result.emplace(std::make_pair(std::move(it->first), std::move(it->second.value)));
                values_.erase(it);                
            }
            return result;
        }
    };
}

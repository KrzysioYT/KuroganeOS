#pragma once

#include "status.hpp"

namespace libk {

struct ListNode {
    ListNode* previous = nullptr;
    ListNode* next = nullptr;
};

class List {
public:
    bool empty() const { return head_ == nullptr; }
    ListNode* front() const { return head_; }
    ListNode* back() const { return tail_; }

    KStatus push_back(ListNode* node) {
        if (node == nullptr || node->previous != nullptr || node->next != nullptr ||
            node == head_ || node == tail_) {
            return KStatus::InvalidArgument;
        }
        node->previous = tail_;
        if (tail_ != nullptr) {
            tail_->next = node;
        } else {
            head_ = node;
        }
        tail_ = node;
        return KStatus::Ok;
    }

    KStatus remove(ListNode* node) {
        if (node == nullptr ||
            (node != head_ && node != tail_ &&
             node->previous == nullptr && node->next == nullptr)) {
            return KStatus::NotFound;
        }
        if (node->previous != nullptr) {
            node->previous->next = node->next;
        } else if (head_ == node) {
            head_ = node->next;
        } else {
            return KStatus::NotFound;
        }
        if (node->next != nullptr) {
            node->next->previous = node->previous;
        } else if (tail_ == node) {
            tail_ = node->previous;
        } else {
            return KStatus::NotFound;
        }
        node->previous = nullptr;
        node->next = nullptr;
        return KStatus::Ok;
    }

private:
    ListNode* head_ = nullptr;
    ListNode* tail_ = nullptr;
};

} // namespace libk

#pragma once

#include <stddef.h>

#include "status.hpp"

namespace libk {

template<typename T, size_t Capacity>
class RingBuffer {
    static_assert(Capacity != 0, "RingBuffer capacity must be non-zero");

public:
    KStatus push(const T& value) {
        if (full()) {
            return KStatus::Busy;
        }
        values_[tail_] = value;
        tail_ = (tail_ + 1U) % Capacity;
        ++size_;
        return KStatus::Ok;
    }

    KStatus pop(T* value) {
        if (value == nullptr) {
            return KStatus::InvalidArgument;
        }
        if (empty()) {
            return KStatus::WouldBlock;
        }
        *value = values_[head_];
        head_ = (head_ + 1U) % Capacity;
        --size_;
        return KStatus::Ok;
    }

    bool empty() const { return size_ == 0; }
    bool full() const { return size_ == Capacity; }
    size_t size() const { return size_; }
    constexpr size_t capacity() const { return Capacity; }

private:
    T values_[Capacity]{};
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t size_ = 0;
};

} // namespace libk

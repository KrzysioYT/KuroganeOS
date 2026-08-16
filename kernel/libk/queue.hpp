#pragma once

#include "ring_buffer.hpp"

namespace libk {

template<typename T, size_t Capacity>
class Queue {
public:
    KStatus enqueue(const T& value) { return storage_.push(value); }
    KStatus dequeue(T* value) { return storage_.pop(value); }
    bool empty() const { return storage_.empty(); }
    bool full() const { return storage_.full(); }
    size_t size() const { return storage_.size(); }

private:
    RingBuffer<T, Capacity> storage_{};
};

} // namespace libk

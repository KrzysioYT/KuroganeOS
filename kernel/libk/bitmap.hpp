#pragma once

#include <stddef.h>
#include <stdint.h>

#include "status.hpp"

namespace libk {

class Bitmap {
public:
    KStatus initialize(uint64_t* words, size_t word_count, size_t bit_count) {
        if (words == nullptr || word_count == 0 || bit_count == 0 ||
            bit_count > word_count * 64U) {
            return KStatus::InvalidArgument;
        }
        words_ = words;
        word_count_ = word_count;
        bit_count_ = bit_count;
        return KStatus::Ok;
    }

    KStatus set(size_t index, bool value = true) {
        if (index >= bit_count_) {
            return KStatus::OutOfRange;
        }
        const uint64_t mask = UINT64_C(1) << (index % 64U);
        if (value) {
            words_[index / 64U] |= mask;
        } else {
            words_[index / 64U] &= ~mask;
        }
        return KStatus::Ok;
    }

    KStatus test(size_t index, bool* value) const {
        if (value == nullptr) {
            return KStatus::InvalidArgument;
        }
        *value = false;
        if (index >= bit_count_) {
            return KStatus::OutOfRange;
        }
        *value = (words_[index / 64U] &
            (UINT64_C(1) << (index % 64U))) != 0;
        return KStatus::Ok;
    }

    KStatus find_first_clear(size_t* index) const {
        if (index == nullptr) {
            return KStatus::InvalidArgument;
        }
        *index = 0;
        for (size_t candidate = 0; candidate < bit_count_; ++candidate) {
            if ((words_[candidate / 64U] &
                 (UINT64_C(1) << (candidate % 64U))) == 0) {
                *index = candidate;
                return KStatus::Ok;
            }
        }
        return KStatus::NotFound;
    }

    size_t size() const { return bit_count_; }

private:
    uint64_t* words_ = nullptr;
    size_t word_count_ = 0;
    size_t bit_count_ = 0;
};

} // namespace libk

#include "rtc.hpp"

#include "../arch/x86_64/io.hpp"

#include <stddef.h>

namespace rtc {

namespace {
uint8_t read_register(uint8_t index) {
    // Keep bit 7 clear so a clock read never leaves NMI disabled.
    arch::out8(0x70, index);
    return arch::in8(0x71);
}

bool update_in_progress() {
    return (read_register(0x0A) & 0x80u) != 0;
}

uint8_t from_bcd(uint8_t value) {
    return static_cast<uint8_t>((value & 0x0Fu) + (value >> 4) * 10);
}

bool equal(const DateTime& left, const DateTime& right) {
    return left.year == right.year && left.month == right.month &&
           left.day == right.day && left.hour == right.hour &&
           left.minute == right.minute && left.second == right.second;
}

bool read_once(DateTime& output) {
    size_t spin = 0;
    while (update_in_progress() && spin++ < 100000) {
        arch::pause();
    }
    if (spin >= 100000) {
        return false;
    }

    uint8_t second = read_register(0x00);
    uint8_t minute = read_register(0x02);
    uint8_t hour = read_register(0x04);
    uint8_t day = read_register(0x07);
    uint8_t month = read_register(0x08);
    uint8_t year = read_register(0x09);
    const uint8_t status_b = read_register(0x0B);

    if ((status_b & 0x04u) == 0) {
        second = from_bcd(second);
        minute = from_bcd(minute);
        day = from_bcd(day);
        month = from_bcd(month);
        year = from_bcd(year);
        hour = static_cast<uint8_t>(
            from_bcd(static_cast<uint8_t>(hour & 0x7Fu)) |
            (hour & 0x80u));
    }
    if ((status_b & 0x02u) == 0) {
        const bool pm = (hour & 0x80u) != 0;
        hour &= 0x7Fu;
        if (pm && hour != 12) {
            hour = static_cast<uint8_t>(hour + 12);
        } else if (!pm && hour == 12) {
            hour = 0;
        }
    }

    output.year = static_cast<uint16_t>(2000u + year);
    output.month = month;
    output.day = day;
    output.hour = hour;
    output.minute = minute;
    output.second = second;
    return month >= 1 && month <= 12 && day >= 1 && day <= 31 &&
           hour <= 23 && minute <= 59 && second <= 59;
}
} // namespace

bool read(DateTime& output) {
    DateTime first{};
    DateTime second{};
    for (size_t attempt = 0; attempt < 4; ++attempt) {
        if (!read_once(first) || !read_once(second)) {
            return false;
        }
        if (equal(first, second)) {
            output = second;
            return true;
        }
    }
    return false;
}

bool is_leap_year(uint16_t year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

} // namespace rtc

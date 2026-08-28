#include "rtc.hpp"

#include "../arch/x86_64/io.hpp"

#include <stddef.h>

namespace rtc {

namespace {
constexpr size_t kUpdateSpinLimit = 250000U;
constexpr size_t kReadAttempts = 16U;
constexpr uint8_t kCenturyRegister = 0x32U;

uint8_t read_register(uint8_t index) {
    // Keep bit 7 clear so a clock read never leaves NMI disabled.
    arch::out8(0x70, index);
    return arch::in8(0x71);
}

bool update_in_progress() {
    return (read_register(0x0A) & 0x80u) != 0;
}

bool wait_for_update_window() {
    size_t spin = 0U;
    while (update_in_progress()) {
        if (++spin >= kUpdateSpinLimit) return false;
        arch::pause();
    }
    return true;
}

uint8_t from_bcd(uint8_t value) {
    return static_cast<uint8_t>((value & 0x0Fu) + (value >> 4) * 10U);
}

bool valid_bcd(uint8_t value) {
    return (value & 0x0Fu) <= 9U && ((value >> 4U) & 0x0Fu) <= 9U;
}

uint8_t days_in_month(uint16_t year, uint8_t month) {
    switch (month) {
        case 1U:
        case 3U:
        case 5U:
        case 7U:
        case 8U:
        case 10U:
        case 12U:
            return 31U;
        case 4U:
        case 6U:
        case 9U:
        case 11U:
            return 30U;
        case 2U:
            return is_leap_year(year) ? 29U : 28U;
        default:
            return 0U;
    }
}

bool decode_snapshot(
    uint8_t second_raw,
    uint8_t minute_raw,
    uint8_t hour_raw,
    uint8_t day_raw,
    uint8_t month_raw,
    uint8_t year_raw,
    uint8_t century_raw,
    uint8_t status_b,
    DateTime& output) {
    const bool binary_mode = (status_b & 0x04U) != 0U;
    const bool twenty_four_hour = (status_b & 0x02U) != 0U;

    if (!binary_mode) {
        const uint8_t hour_digits = static_cast<uint8_t>(hour_raw & 0x7FU);
        if (!valid_bcd(second_raw) || !valid_bcd(minute_raw) ||
            !valid_bcd(hour_digits) || !valid_bcd(day_raw) ||
            !valid_bcd(month_raw) || !valid_bcd(year_raw)) {
            return false;
        }
        if (century_raw != 0U && century_raw != 0xFFU &&
            !valid_bcd(century_raw)) {
            century_raw = 0U;
        }

        second_raw = from_bcd(second_raw);
        minute_raw = from_bcd(minute_raw);
        day_raw = from_bcd(day_raw);
        month_raw = from_bcd(month_raw);
        year_raw = from_bcd(year_raw);
        hour_raw = static_cast<uint8_t>(
            from_bcd(hour_digits) | (hour_raw & 0x80U));
        if (century_raw != 0U && century_raw != 0xFFU) {
            century_raw = from_bcd(century_raw);
        }
    }

    if (!twenty_four_hour) {
        const bool pm = (hour_raw & 0x80U) != 0U;
        hour_raw &= 0x7FU;
        if (hour_raw < 1U || hour_raw > 12U) return false;
        if (pm && hour_raw != 12U) {
            hour_raw = static_cast<uint8_t>(hour_raw + 12U);
        } else if (!pm && hour_raw == 12U) {
            hour_raw = 0U;
        }
    } else {
        hour_raw &= 0x7FU;
    }

    uint16_t full_year = 0U;
    if (century_raw >= 19U && century_raw <= 99U) {
        full_year = static_cast<uint16_t>(
            static_cast<uint16_t>(century_raw) * 100U + year_raw);
    } else {
        // KuroganeOS currently targets modern UEFI machines. If firmware does
        // not expose a usable century register, interpret the two-digit CMOS
        // year in the 2000-2099 window instead of inventing a host timestamp.
        full_year = static_cast<uint16_t>(2000U + year_raw);
    }

    const uint8_t maximum_day = days_in_month(full_year, month_raw);
    if (full_year < 2000U || month_raw < 1U || month_raw > 12U ||
        day_raw < 1U || day_raw > maximum_day || hour_raw > 23U ||
        minute_raw > 59U || second_raw > 59U) {
        return false;
    }

    output.year = full_year;
    output.month = month_raw;
    output.day = day_raw;
    output.hour = hour_raw;
    output.minute = minute_raw;
    output.second = second_raw;
    return true;
}

bool read_stable_snapshot(DateTime& output) {
    if (!wait_for_update_window()) return false;

    // CMOS registers are not latched. Guard the snapshot with the seconds
    // register and UIP so a rollover cannot mix fields from two timestamps.
    const uint8_t second_before = read_register(0x00U);
    const uint8_t minute = read_register(0x02U);
    const uint8_t hour = read_register(0x04U);
    const uint8_t day = read_register(0x07U);
    const uint8_t month = read_register(0x08U);
    const uint8_t year = read_register(0x09U);
    const uint8_t century = read_register(kCenturyRegister);
    const uint8_t status_b = read_register(0x0BU);
    const uint8_t second_after = read_register(0x00U);

    if (update_in_progress() || second_before != second_after) return false;

    return decode_snapshot(
        second_after,
        minute,
        hour,
        day,
        month,
        year,
        century,
        status_b,
        output);
}
} // namespace

bool read(DateTime& output) {
    DateTime snapshot{};
    for (size_t attempt = 0U; attempt < kReadAttempts; ++attempt) {
        if (read_stable_snapshot(snapshot)) {
            output = snapshot;
            return true;
        }
        arch::pause();
    }
    return false;
}

bool is_leap_year(uint16_t year) {
    return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
}

} // namespace rtc

#pragma once

#include <stdint.h>

namespace rtc {

struct DateTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

bool read(DateTime& output);
bool is_leap_year(uint16_t year);

} // namespace rtc

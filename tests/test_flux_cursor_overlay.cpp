#include "../kernel/ui/cursor_sprite.hpp"
#include <cassert>
#include <climits>
#include <cstdint>
int main() {
    using namespace flux_cursor;
    static_assert(kWidth == 16 && kHeight == 21, "cursor dimensions");
    static_assert(sample(0,0) == Ink::Outline, "hotspot");
    static_assert(sample(2,3) == Ink::Fill, "filled arrow");
    static_assert(sample(-1,0) == Ink::Transparent, "negative clip");
    static_assert(sample(kWidth,kHeight) == Ink::Transparent, "far clip");
    uint32_t background[64]{};
    uint32_t pixels[64]{};
    bool saved[kWidth*kHeight]{};
    uint32_t original[kWidth*kHeight]{};
    for (uint32_t i = 0; i < 64; ++i) {
        background[i] = 0x101010U + i;
        pixels[i] = background[i];
    }
    int writes = 0;
    visit_visible(6,6,8,8,[&](uint32_t idx,int32_t x,int32_t y,Ink ink) {
        assert(idx < kWidth*kHeight && ink != Ink::Transparent);
        const uint32_t pos = static_cast<uint32_t>(y*8+x);
        original[idx] = pixels[pos]; saved[idx] = true;
        pixels[pos] = 0xFF0000U;
        ++writes;
    });
    assert(writes > 0 && writes <= 4);
    visit_visible(6,6,8,8,[&](uint32_t idx,int32_t x,int32_t y,Ink) {
        assert(saved[idx]);
        pixels[static_cast<uint32_t>(y*8+x)] = original[idx];
        saved[idx] = false;
    });
    for (uint32_t i = 0; i < 64; ++i) assert(pixels[i] == background[i]);
    int bottom_right = 0;
    visit_visible(INT32_MAX-5,INT32_MAX-5,8,8,
        [&](uint32_t,int32_t,int32_t,Ink){++bottom_right;});
    assert(bottom_right == 0);
    int offscreen = 0;
    visit_visible(-100,-100,8,8,[&](uint32_t,int32_t,int32_t,Ink){++offscreen;});
    assert(offscreen == 0);
    int origin = 0;
    visit_visible(0,0,8,8,[&](uint32_t,int32_t,int32_t,Ink){++origin;});
    assert(origin > 0);
    return 0;
}

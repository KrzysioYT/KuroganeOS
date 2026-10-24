#include "../kernel/drivers/rounded_rect.hpp"
#include <cassert>
int main() {
    using graphics::raster::rounded_inset;
    static_assert(rounded_inset(8,0) == 7, "rounded top");
    static_assert(rounded_inset(8,7) == 0, "rounded center");
    static_assert(rounded_inset(1,0) == 0, "one pixel corner");
    for (int r = 2; r <= 16; ++r) {
        int previous = r;
        for (int y = 0; y < r; ++y) {
            const int inset = rounded_inset(r,y);
            assert(inset >= 0 && inset <= previous);
            previous = inset;
        }
    }
    static_assert(graphics::raster::coverage4x4(8, 0, 0) == 0U,
                  "empty extreme corner");
    static_assert(graphics::raster::coverage4x4(8, 7, 7) == 16U,
                  "full inner corner");
    bool found_antialiasing = false;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            const auto value = graphics::raster::coverage4x4(8, x, y);
            assert(value <= 16U);
            if (value != 0U && value != 16U) found_antialiasing = true;
            assert(value == graphics::raster::coverage4x4(8, y, x));
        }
    assert(found_antialiasing);
    assert(rounded_inset(8,-1) == 0);
    assert(rounded_inset(8,8) == 0);
    return 0;
}

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
    assert(rounded_inset(8,-1) == 0);
    assert(rounded_inset(8,8) == 0);
    return 0;
}

#include "../kernel/user/console.hpp"

#include <cassert>

int main() {
    using namespace user::console;
    char value = 0;
    assert(!active());
    assert(!push('x'));
    initialize();
    assert(active() && pending() == 0U && dropped() == 0U);
    assert(push('a') && push('b'));
    assert(pending() == 2U);
    assert(try_read(&value) && value == 'a');
    assert(try_read(&value) && value == 'b');
    assert(!try_read(&value));
    for (size_t index = 0U; index < INPUT_CAPACITY; ++index) {
        assert(push('z'));
    }
    assert(!push('!') && dropped() == 1U);
    shutdown();
    assert(!active() && pending() == 0U);
    return 0;
}

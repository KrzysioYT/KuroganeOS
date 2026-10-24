#include "../kernel/ui/text_fit.hpp"
#include <cassert>
#include <cstring>
int main() {
    char buffer[32]{};
    using ui::text_fit::fit_ascii;
    fit_ascii("SETTINGS", buffer, sizeof(buffer), 8U);
    assert(std::strcmp(buffer, "SETTINGS") == 0);
    fit_ascii("KUROGANE WEB / BROWSER", buffer, sizeof(buffer), 12U);
    assert(std::strcmp(buffer, "KUROGANE ...") == 0);
    fit_ascii("HOME", buffer, sizeof(buffer), 3U);
    assert(std::strcmp(buffer, "HOM") == 0);
    fit_ascii("A VERY LONG LABEL", buffer, 5U, 50U);
    assert(std::strcmp(buffer, "A...") == 0);
    fit_ascii("LINE\\nTWO", buffer, sizeof(buffer), 20U);
    assert(std::strcmp(buffer, "LINE\\nTWO") == 0);
    fit_ascii("HELLO\nWORLD", buffer, sizeof(buffer), 20U);
    assert(std::strcmp(buffer, "HELLO") == 0);
    fit_ascii(nullptr, buffer, sizeof(buffer), 12U);
    assert(buffer[0] == '\0');
    fit_ascii("anything", buffer, sizeof(buffer), 0U);
    assert(buffer[0] == '\0');
    fit_ascii("anything", nullptr, 0U, 12U);
    return 0;
}

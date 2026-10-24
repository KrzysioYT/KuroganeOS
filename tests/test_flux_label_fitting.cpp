#include "../kernel/ui/text_fit.hpp"
#include <cassert>
#include <cstring>

int main() {
    using ui::text_fit::fit_utf8;
    using ui::text_fit::fit_ascii;
    using ku_text::next_z;
    char b[64]{};
    fit_ascii("SETTINGS", b, sizeof(b), 8U);
    assert(std::strcmp(b,"SETTINGS")==0);
    fit_ascii("KUROGANE WEB / BROWSER", b, sizeof(b), 12U);
    assert(std::strcmp(b,"KUROGANE ...")==0);
    fit_ascii("HOME", b, sizeof(b), 3U);
    assert(std::strcmp(b,"HOM")==0);
    fit_utf8("Język polski", b, sizeof(b), 7U);
    assert(std::strcmp(b,"Języ...")==0);
    fit_utf8("Zażółć gęślą jaźń", b, sizeof(b), 64U);
    assert(std::strcmp(b,"Zażółć gęślą jaźń")==0);
    fit_utf8("Zażółć gęślą jaźń", b, 9U, 40U);
    assert(std::strcmp(b,"Zaż...")==0);
    fit_utf8("Łódź\nnext", b, sizeof(b), 30U);
    assert(std::strcmp(b,"Łódź")==0);
    fit_utf8(nullptr, b, sizeof(b), 10U);
    assert(b[0]=='\0');
    fit_utf8("anything", b, sizeof(b), 0U);
    assert(b[0]=='\0');
    fit_utf8("anything", nullptr, 0U, 10U);
    const char invalid[] = {'A', static_cast<char>(0xc3), 'B', 0};
    fit_utf8(invalid, b, sizeof(b), 10U);
    assert(std::strcmp(b,"A?B")==0);
    assert(next_z("\xC0\xAF").valid == false); // overlong
    assert(next_z("\xED\xA0\x80").valid == false); // surrogate
    assert(next_z("\xF4\x90\x80\x80").valid == false); // > U+10FFFF
    assert(next_z("\xE2").valid == false); // truncated
    assert(next_z("ł").value == 0x0142U);
    assert(ku_text::fallback(0x0142U).accent == ku_text::Accent::Stroke);
    assert(ku_text::fallback(0x1f600U).ascii == '?');
    return 0;
}

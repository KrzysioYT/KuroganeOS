#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "../kernel/libk/atomic.hpp"
#include "../kernel/libk/bitmap.hpp"
#include "../kernel/libk/crc.hpp"
#include "../kernel/libk/format.hpp"
#include "../kernel/libk/hash.hpp"
#include "../kernel/libk/list.hpp"
#include "../kernel/libk/math.hpp"
#include "../kernel/libk/memory.hpp"
#include "../kernel/libk/queue.hpp"
#include "../kernel/libk/ring_buffer.hpp"
#include "../kernel/libk/status.hpp"
#include "../kernel/libk/string.hpp"
#include "../kernel/libk/utf8.hpp"

int main() {
    assert(kstatus_succeeded(KStatus::Ok));
    assert(kstatus_failed(KStatus::IoError));
    assert(k_strcmp(kstatus_name(KStatus::Timeout), "KSTATUS_TIMEOUT") == 0);

    char bytes[8]{};
    k_memset(bytes, 'x', 4);
    k_memmove(bytes + 1, bytes, 4);
    assert(k_memcmp(bytes, "xxxxx", 5) == 0);
    assert(k_strlen("Kurogane") == 8);
    assert(k_strncmp("kernel", "kern", 4) == 0);

    char formatted[32];
    assert(k_snprintf(formatted, sizeof(formatted), "%s:%04X:%d", "io", 42U, -7) == 10);
    assert(std::strcmp(formatted, "io:002A:-7") == 0);
    char truncated[5];
    assert(k_snprintf(truncated, sizeof(truncated), "abcdef") == 6);
    assert(std::strcmp(truncated, "abcd") == 0);

    uint64_t words[2]{};
    libk::Bitmap bitmap;
    assert(bitmap.initialize(words, 2, 65) == KStatus::Ok);
    assert(bitmap.set(0) == KStatus::Ok);
    size_t free_bit = 99;
    assert(bitmap.find_first_clear(&free_bit) == KStatus::Ok && free_bit == 1);

    libk::List list;
    libk::ListNode first{};
    libk::ListNode second{};
    assert(list.push_back(&first) == KStatus::Ok);
    assert(list.push_back(&second) == KStatus::Ok);
    assert(list.front() == &first && list.back() == &second);
    assert(list.remove(&first) == KStatus::Ok && list.front() == &second);

    libk::RingBuffer<int, 2> ring;
    assert(ring.push(10) == KStatus::Ok);
    assert(ring.push(20) == KStatus::Ok);
    assert(ring.push(30) == KStatus::Busy);
    int value = 0;
    assert(ring.pop(&value) == KStatus::Ok && value == 10);
    assert(ring.push(30) == KStatus::Ok);

    libk::Queue<int, 2> queue;
    assert(queue.enqueue(7) == KStatus::Ok);
    assert(queue.dequeue(&value) == KStatus::Ok && value == 7);

    assert(k_crc32("123456789", 9) == UINT32_C(0xCBF43926));
    assert(k_fnv1a64("hello", 5) == UINT64_C(0xA430D84680AABD0B));

    const char utf8[] = "Kuro\xE9\xBB\x92";
    assert(k_utf8_validate(utf8, sizeof(utf8) - 1) == KStatus::Ok);
    const char invalid[] = {static_cast<char>(0xC0), static_cast<char>(0x80)};
    assert(k_utf8_validate(invalid, sizeof(invalid)) == KStatus::Corrupted);

    uint64_t aligned = 0;
    assert(libk::align_up<uint64_t>(17, 16, &aligned) && aligned == 32);
    libk::Atomic<uint32_t> atomic(1);
    uint32_t expected = 1;
    assert(atomic.compare_exchange(expected, 2));
    assert(atomic.load() == 2);

    std::cout << "libk tests: PASS\n";
    return 0;
}

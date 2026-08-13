#include "mem.h"
#include "os.h"
#include "types.h"

using engine_placeholder::u8;
using engine_placeholder::usize;
using engine_placeholder::os::Exit;
using engine_placeholder::os::Write;

namespace {

void Expect(bool cond, const char* label) {
    Write(1, label, strlen(label));
    if (cond) {
        const char pass[] = ": PASS\n";
        Write(1, pass, sizeof(pass) - 1);
    } else {
        const char fail[] = ": FAIL\n";
        Write(1, fail, sizeof(fail) - 1);
        Exit(1);
    }
}

bool BytesEqual(const u8* a, const u8* b, usize n) {
    for (usize i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

void TestMemcpy() {
    {
        u8 src[1] = {0xAA};
        u8 dst[1] = {0x00};
        memcpy(dst, src, 0);
        Expect(dst[0] == 0x00, "memcpy size 0 is a no-op");
    }
    {
        u8 src[1] = {0x42};
        u8 dst[1] = {0x00};
        memcpy(dst, src, 1);
        Expect(dst[0] == 0x42, "memcpy size 1");
    }
    {
        u8 src[7] = {1, 2, 3, 4, 5, 6, 7};
        u8 dst[7] = {0, 0, 0, 0, 0, 0, 0};
        memcpy(dst, src, 7);
        Expect(BytesEqual(dst, src, 7), "memcpy size 7 (unaligned length)");
    }
    {
        // 40-byte source buffer; copy 32 bytes starting at a misaligned
        // (odd) offset to exercise unaligned pointers.
        u8 buf[40];
        for (usize i = 0; i < 40; ++i) {
            buf[i] = static_cast<u8>(i);
        }
        u8 dst[32] = {};
        memcpy(dst, buf + 3, 32);
        Expect(BytesEqual(dst, buf + 3, 32), "memcpy size 32 (misaligned src)");
    }
}

void TestMemset() {
    {
        u8 buf[10] = {};
        memset(buf, 0xAB, 10);
        bool ok = true;
        for (usize i = 0; i < 10; ++i) {
            ok = ok && buf[i] == 0xAB;
        }
        Expect(ok, "memset size 10");
    }
    {
        u8 buf[1] = {0x11};
        memset(buf, 0, 0);
        Expect(buf[0] == 0x11, "memset size 0 is a no-op");
    }
    {
        // Odd byte count at an odd offset into a larger buffer.
        u8 buf[16];
        for (usize i = 0; i < 16; ++i) {
            buf[i] = 0;
        }
        memset(buf + 3, 0xFF, 9);
        bool ok = true;
        for (usize i = 0; i < 3; ++i) {
            ok = ok && buf[i] == 0;
        }
        for (usize i = 3; i < 12; ++i) {
            ok = ok && buf[i] == 0xFF;
        }
        for (usize i = 12; i < 16; ++i) {
            ok = ok && buf[i] == 0;
        }
        Expect(ok, "memset size 9 (misaligned offset)");
    }
}

void TestMemmove() {
    {
        // Forward overlap: dest > src.
        // buf = {0,1,2,3,4,5,6,7,8,9}; memmove(buf+2, buf, 6)
        // hand-computed: temp = buf[0..5] = {0,1,2,3,4,5}
        //   buf[2..7] = temp; buf[0],[1] and buf[8],[9] untouched
        //   => {0,1,0,1,2,3,4,5,8,9}
        u8 buf[10];
        for (usize i = 0; i < 10; ++i) {
            buf[i] = static_cast<u8>(i);
        }
        memmove(buf + 2, buf, 6);
        const u8 expected[10] = {0, 1, 0, 1, 2, 3, 4, 5, 8, 9};
        Expect(BytesEqual(buf, expected, 10), "memmove forward overlap (dest > src)");
    }
    {
        // Backward overlap: dest < src.
        // buf = {0,1,2,3,4,5,6,7,8,9}; memmove(buf, buf+2, 6)
        // hand-computed: temp = buf[2..7] = {2,3,4,5,6,7}
        //   buf[0..5] = temp; buf[6..9] unchanged (6,7,8,9; the source
        //   copy already captured old buf[6],buf[7] before they'd be
        //   read again, so they're untouched here since dest never
        //   reaches index 6)
        //   => {2,3,4,5,6,7,6,7,8,9}
        u8 buf[10];
        for (usize i = 0; i < 10; ++i) {
            buf[i] = static_cast<u8>(i);
        }
        memmove(buf, buf + 2, 6);
        const u8 expected[10] = {2, 3, 4, 5, 6, 7, 6, 7, 8, 9};
        Expect(BytesEqual(buf, expected, 10), "memmove backward overlap (dest < src)");
    }
    {
        // Non-overlapping regions behave like memcpy.
        u8 buf[12];
        for (usize i = 0; i < 12; ++i) {
            buf[i] = static_cast<u8>(i);
        }
        u8 expected[6] = {0, 1, 2, 3, 4, 5};
        memmove(buf + 6, buf, 6);
        Expect(BytesEqual(buf + 6, expected, 6), "memmove non-overlapping");
    }
}

void TestStrlen() {
    Expect(strlen("") == 0, "strlen empty string");
    Expect(strlen("hi") == 2, "strlen \"hi\"");
    Expect(strlen("Hello, world!") == 13, "strlen \"Hello, world!\"");
}

} // namespace

int main() {
    TestMemcpy();
    TestMemset();
    TestMemmove();
    TestStrlen();

    const char ok[] = "OK\n";
    Write(1, ok, sizeof(ok) - 1);
    return 0;
}

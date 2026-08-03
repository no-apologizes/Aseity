#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

typedef unsigned __int128 u128;
typedef __int128 i128;

// Bools (i1)
void aseity_print_bool(uint8_t val) {
    if (val) {
        write(STDOUT_FILENO, "true\n", 5);
    } else {
        write(STDOUT_FILENO, "false\n", 6);
    }
}

// Strings (str / i8*)
void aseity_print_str(const char *s) {
    if (!s) s = "(null)";
    size_t len = strlen(s);
    write(STDOUT_FILENO, s, len);
    write(STDOUT_FILENO, "\n", 1);
}

// UTF-8 Codepoints (char / u32)
void aseity_print_utf8(uint32_t codepoint) {
    uint8_t utf8_buf[5];
    int len = 0;

    if (codepoint <= 0x7F) {
        utf8_buf[0] = (uint8_t)codepoint;
        len = 1;
    } else if (codepoint <= 0x7FF) {
        utf8_buf[0] = (uint8_t)(0xC0 | (codepoint >> 6));
        utf8_buf[1] = (uint8_t)(0x80 | (codepoint & 0x3F));
        len = 2;
    } else if (codepoint <= 0xFFFF) {
        utf8_buf[0] = (uint8_t)(0xE0 | (codepoint >> 12));
        utf8_buf[1] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
        utf8_buf[2] = (uint8_t)(0x80 | (codepoint & 0x3F));
        len = 3;
    } else if (codepoint <= 0x10FFFF) {
        utf8_buf[0] = (uint8_t)(0xF0 | (codepoint >> 18));
        utf8_buf[1] = (uint8_t)(0x80 | ((codepoint >> 12) & 0x3F));
        utf8_buf[2] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
        utf8_buf[3] = (uint8_t)(0x80 | (codepoint & 0x3F));
        len = 4;
    }

    utf8_buf[len++] = '\n';
    write(STDOUT_FILENO, utf8_buf, (size_t)len);
}

// 64-Bit Signed Integers (i64)
void aseity_print_i64(int64_t val) {
    char buf[25];
    int i = 23;
    buf[24] = '\0';
    buf[23] = '\n';

    int is_negative = 0;
    uint64_t uval;

    if (val < 0) {
        is_negative = 1;
        uval = (uint64_t)(-val);
    } else {
        uval = (uint64_t)val;
    }

    if (uval == 0) {
        buf[--i] = '0';
    } else {
        while (uval > 0) {
            buf[--i] = (char)('0' + (uval % 10));
            uval /= 10;
        }
    }

    if (is_negative) {
        buf[--i] = '-';
    }

    write(STDOUT_FILENO, &buf[i], (size_t)(24 - i));
}

// Floating-Points (f64)
void aseity_print_f64(double val) {
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "%g\n", val);
    if (len > 0) {
        write(STDOUT_FILENO, buf, (size_t)len);
    }
}

// 128-Bit Signed Integers (i128)
void aseity_print_i128(i128 val) {
    char buf[45];
    int i = 43;
    buf[44] = '\0';
    buf[43] = '\n';

    int is_negative = 0;
    u128 uval;

    if (val < 0) {
        is_negative = 1;
        uval = (u128)(-val);
    } else {
        uval = (u128)val;
    }

    if (uval == 0) {
        buf[--i] = '0';
    } else {
        while (uval > 0) {
            buf[--i] = (char)('0' + (uval % 10));
            uval /= 10;
        }
    }

    if (is_negative) {
        buf[--i] = '-';
    }

    write(STDOUT_FILENO, &buf[i], (size_t)(44 - i));
}

// 128-Bit Unsigned Integers (u128)
void aseity_print_u128(u128 val) {
    char buf[45];
    int i = 43;
    buf[44] = '\0';
    buf[43] = '\n';

    if (val == 0) {
        buf[--i] = '0';
    } else {
        while (val > 0) {
            buf[--i] = (char)('0' + (val % 10));
            val /= 10;
        }
    }

    write(STDOUT_FILENO, &buf[i], (size_t)(44 - i));
}

// 64-Bit Memory Allocator Intrinsic (mmap)
int64_t aseity_mem_alloc(int64_t size) {
    // MAP_PRIVATE | MAP_ANONYMOUS requests zero-initialized memory completely independent of any file
    void *ptr = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (ptr == MAP_FAILED) {
        return 0; // Return 0 to represent a null pointer/out of memory
    }

    return (int64_t)ptr;
}

// Allows Aseity to pass an i64 memory address and print it as a null-terminated string
void print_str(int64_t ptr) {
    if (ptr == 0) return;
    aseity_print_str((const char *)ptr);
}

// Opens a file descriptor (Read-Only)
int64_t file_open(const char *path) {
    return open(path, O_RDONLY);
}

// Determines file size using lseek, then rewinds the cursor to 0
int64_t file_size(int64_t fd) {
    if (fd < 0) return -1;
    int64_t size = lseek((int)fd, 0, SEEK_END);
    lseek((int)fd, 0, SEEK_SET);
    return size;
}

// Reads 'size' bytes into the raw pointer buffer
int64_t file_read(int64_t fd, int64_t buffer_ptr, int64_t size) {
    if (fd < 0 || buffer_ptr == 0) return -1;
    return read((int)fd, (void*)buffer_ptr, (size_t)size);
}

// Closes the file descriptor
void file_close(int64_t fd) {
    if (fd >= 0) close((int)fd);
}
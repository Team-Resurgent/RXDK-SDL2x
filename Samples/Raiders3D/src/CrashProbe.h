#pragma once
#include <xtl.h>

extern "C" long g__rx_tu_seq;

static inline void RXDBG(const char* s) { OutputDebugStringA(s); }

// Minimal, no snprintf needed on Xbox
static inline void RXPRINT_TU(const char* file, long n) {
    char buf[512];
    wsprintfA(buf, "[TUProbe #%ld] %s\r\n", n, file);
    RXDBG(buf);
}

struct __RX_TUProbe {
    __RX_TUProbe(const char* file) {
        long n = InterlockedIncrement(&g__rx_tu_seq);
        RXPRINT_TU(file, n);
    }
};

// Put this *once per .cpp* right after includes:
#define PROBE_THIS_TU namespace{ static __RX_TUProbe __tu_probe(__FILE__); }

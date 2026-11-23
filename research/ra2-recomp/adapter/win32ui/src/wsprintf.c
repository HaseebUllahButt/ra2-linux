/* wsprintf.c: wsprintfA (cdecl, 1024-byte limit, no floating point). */
#include "ui.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
INT ui_wvsprintfA(char *out, const char *fmt, va_list ap)
{
    char *o = out, *end = out + 1023;
    while (*fmt && o < end) {
        if (*fmt != '%') { *o++ = *fmt++; continue; }
        const char *start = fmt++;
        if (*fmt == '%') { *o++ = '%'; fmt++; continue; }
        char spec[32]; int k = 0; spec[k++] = '%';
        while (*fmt == '-' || *fmt == '#' || *fmt == '0') spec[k++] = *fmt++;
        while (*fmt >= '0' && *fmt <= '9' && k < 20) spec[k++] = *fmt++;
        if (*fmt == '.') { spec[k++] = *fmt++; while (*fmt >= '0' && *fmt <= '9' && k < 26) spec[k++] = *fmt++; }
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'w' || *fmt == 'I') fmt++;
        char c = *fmt;
        if (!c) break;
        fmt++;
        char tmp[1100]; int n;
        switch (c) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'c':
            spec[k++] = c == 'i' ? 'd' : c; spec[k] = 0;
            n = snprintf(tmp, sizeof tmp, spec, va_arg(ap, int)); break;
        case 's': case 'S': {
            const char *s = va_arg(ap, const char *);
            spec[k++] = 's'; spec[k] = 0;
            n = snprintf(tmp, sizeof tmp, spec, s ? s : "(null)"); break;
        }
        case 'p': { spec[k++] = '0'; spec[k++] = '8'; spec[k++] = 'X'; spec[k] = 0; n = snprintf(tmp, sizeof tmp, "%08X", va_arg(ap, unsigned)); break; }
        default: (void)start; n = 0; break;
        }
        if (n < 0) n = 0;
        if (n > (int)sizeof tmp - 1) n = sizeof tmp - 1;
        for (int i = 0; i < n && o < end; i++) *o++ = tmp[i];
    }
    *o = 0;
    return (INT)(o - out);
}
INT UICDECL ui_wsprintfA(char *out, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    INT r = ui_wvsprintfA(out, fmt, ap);
    va_end(ap);
    return r;
}

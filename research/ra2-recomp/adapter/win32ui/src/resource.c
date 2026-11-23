/* resource.c: find a dialog template in a mapped PE image (game.exe at its load address). */
#include "ui.h"
#include <string.h>
static const BYTE *find_entry(const BYTE *base, const BYTE *dir, const BYTE *root, DWORD id, const char *name)
{
    WORD nn = *(const WORD *)(dir + 12), ni = *(const WORD *)(dir + 14);
    const DWORD *e = (const DWORD *)(dir + 16);
    for (int i = 0; i < nn + ni; i++, e += 2) {
        DWORD n = e[0], off = e[1];
        int hit = 0;
        if (name && (n & 0x80000000u)) {
            const WORD *s = (const WORD *)(root + (n & 0x7fffffffu));
            size_t len = s[0];
            if (len == strlen(name)) {
                hit = 1;
                for (size_t k = 0; k < len; k++) {
                    WORD a = s[1 + k], b = (BYTE)name[k];
                    if (a >= 'a' && a <= 'z') a -= 32;
                    if (b >= 'a' && b <= 'z') b -= 32;
                    if (a != b) { hit = 0; break; }
                }
            }
        } else if (!name && !(n & 0x80000000u) && (id == 0xffffffffu || n == id)) hit = 1;
        if (hit) return root + (off & 0x7fffffffu);
    }
    (void)base;
    return NULL;
}
const void *ui_find_dialog_resource(HINSTANCE inst, const char *name)
{
    const BYTE *base = (const BYTE *)(uintptr_t)(inst ? inst : 0x400000u);
    if (base[0] != 'M' || base[1] != 'Z') return NULL;
    const BYTE *pe = base + *(const DWORD *)(base + 0x3c);
    DWORD rva = *(const DWORD *)(pe + 24 + 96 + 2 * 8);
    if (!rva) return NULL;
    const BYTE *root = base + rva;
    const BYTE *t = find_entry(base, root, root, 5, NULL);   /* RT_DIALOG */
    if (!t) return NULL;
    const BYTE *n = IS_INTRESOURCE(name) ? find_entry(base, t, root, (DWORD)(uintptr_t)name, NULL)
                                         : find_entry(base, t, root, 0, name[0] == '#' ? NULL : name);
    if (!n && !IS_INTRESOURCE(name) && name[0] == '#') {
        DWORD id = 0; for (const char *p = name + 1; *p; p++) id = id * 10 + (DWORD)(*p - '0');
        n = find_entry(base, t, root, id, NULL);
    }
    if (!n) return NULL;
    const BYTE *l = find_entry(base, n, root, 0xffffffffu, NULL);   /* first language */
    if (!l) return NULL;
    DWORD data_rva = *(const DWORD *)l;
    return base + data_rva;
}

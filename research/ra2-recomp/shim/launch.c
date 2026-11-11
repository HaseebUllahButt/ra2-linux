/* rclaunch.exe: start the original game.exe with rc.dll loaded before the game's own code.
 * usage: rclaunch.exe <game.exe> <dll>[;<dll>...] [game arguments...]   (DLLs load in this order)
 * The game starts suspended. Its entry point gets "push stub; ret". The stub loads
 * rc.dll through the game's own LoadLibraryA import slot, puts the entry bytes back
 * and jumps to the entry. The launcher waits for the game and returns its exit code. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static unsigned char *readall(const char *p, DWORD *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(*n); fread(b, 1, *n, f); fclose(f); return b;
}
static DWORD rva2off(unsigned char *d, DWORD rva)
{
    DWORD pe = *(DWORD *)(d + 0x3c); WORD ns = *(WORD *)(d + pe + 6), opt = *(WORD *)(d + pe + 20);
    unsigned char *s = d + pe + 24 + opt;
    for (int i = 0; i < ns; i++, s += 40) {
        DWORD va = *(DWORD *)(s + 12), vs = *(DWORD *)(s + 8), ro = *(DWORD *)(s + 20);
        if (rva >= va && rva < va + vs) return rva - va + ro;
    }
    return 0;
}
int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: rclaunch game.exe rc.dll [args]\n"); return 2; }
    DWORD n; unsigned char *d = readall(argv[1], &n);
    if (!d) { fprintf(stderr, "rclaunch: cannot read %s\n", argv[1]); return 2; }
    DWORD pe = *(DWORD *)(d + 0x3c);
    DWORD entry_rva = *(DWORD *)(d + pe + 40), base = *(DWORD *)(d + pe + 52);
    DWORD imp = *(DWORD *)(d + pe + 24 + 104);
    DWORD llslot = 0;
    for (unsigned char *p = d + rva2off(d, imp); *(DWORD *)(p + 12); p += 20) {
        const char *dll = (const char *)d + rva2off(d, *(DWORD *)(p + 12));
        if (_stricmp(dll, "kernel32.dll")) continue;
        DWORD olt = *(DWORD *)p, iat = *(DWORD *)(p + 16);
        DWORD *t = (DWORD *)(d + rva2off(d, olt ? olt : iat));
        for (int i = 0; t[i]; i++)
            if (!(t[i] & 0x80000000) && !strcmp((char *)d + rva2off(d, t[i]) + 2, "LoadLibraryA")) llslot = base + iat + 4 * i;
    }
    if (!llslot) { fprintf(stderr, "rclaunch: no LoadLibraryA import\n"); return 2; }
    DWORD entry = base + entry_rva;

    char dlls[8][MAX_PATH]; int nd = 0;
    { char list[4096]; strncpy(list, argv[2], sizeof list - 1); list[sizeof list - 1] = 0;
      for (char *t = strtok(list, ";"); t && nd < 8; t = strtok(0, ";")) GetFullPathNameA(t, MAX_PATH, dlls[nd++], 0); }
    char cmd[4096]; snprintf(cmd, sizeof cmd, "\"%s\"", argv[1]);
    for (int i = 3; i < argc; i++) { strncat(cmd, " ", sizeof cmd - strlen(cmd) - 1); strncat(cmd, argv[i], sizeof cmd - strlen(cmd) - 1); }
    STARTUPINFOA si = { sizeof si }; PROCESS_INFORMATION pi;
    if (!CreateProcessA(argv[1], cmd, 0, 0, FALSE, CREATE_SUSPENDED, 0, 0, &si, &pi)) {
        fprintf(stderr, "rclaunch: CreateProcess failed %lu\n", GetLastError()); return 2;
    }
    HANDLE h = pi.hProcess;
    unsigned char *mem = VirtualAllocEx(h, 0, 8192, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    DWORD m = (DWORD)(uintptr_t)mem, saved = m + 0x200, path = m + 0x400;
    unsigned char orig[16]; SIZE_T got;
    DWORD old; VirtualProtectEx(h, (void *)(uintptr_t)entry, 16, PAGE_EXECUTE_READWRITE, &old);
    ReadProcessMemory(h, (void *)(uintptr_t)entry, orig, 16, &got);
    unsigned char st[256]; int k = 0;
#define B(x) st[k++] = (unsigned char)(x)
#define D(x) do { DWORD v_ = (x); memcpy(st + k, &v_, 4); k += 4; } while (0)
    B(0x60);                                  /* pushad */
    for (int i = 0; i < nd; i++) {
        B(0x68); D(path + MAX_PATH * i);      /* push path */
        B(0xff); B(0x15); D(llslot);          /* call [LoadLibraryA] */
    }
    B(0xbe); D(saved);                        /* mov esi, saved */
    B(0xbf); D(entry);                        /* mov edi, entry */
    B(0xb9); D(16);                           /* mov ecx, 16 */
    B(0xfc); B(0xf3); B(0xa4);                /* cld; rep movsb */
    B(0x61);                                  /* popad */
    B(0x68); D(entry); B(0xc3);               /* push entry; ret */
    WriteProcessMemory(h, mem, st, k, &got);
    for (int i = 0; i < nd; i++) WriteProcessMemory(h, (void *)(uintptr_t)(path + MAX_PATH * i), dlls[i], strlen(dlls[i]) + 1, &got);
    WriteProcessMemory(h, (void *)(uintptr_t)saved, orig, 16, &got);
    unsigned char jmp[6] = { 0x68, 0, 0, 0, 0, 0xc3 }; memcpy(jmp + 1, &m, 4);
    WriteProcessMemory(h, (void *)(uintptr_t)entry, jmp, 6, &got);
    FlushInstructionCache(h, 0, 0);
    ResumeThread(pi.hThread);
    WaitForSingleObject(h, INFINITE);
    DWORD code = 0; GetExitCodeProcess(h, &code);
    return (int)code;
}

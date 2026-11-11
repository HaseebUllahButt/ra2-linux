#include <windows.h>
#include <stdio.h>
int main(void)
{
    HMODULE h = LoadLibraryA("rt2.dll");
    if (!h) { printf("FAIL: load rt2.dll (%lu)\n", GetLastError()); return 1; }
    int (*run)(void) = (int (*)(void))GetProcAddress(h, "rt2_run");
    int bad = run();
    printf(bad ? "FAIL: %d errors\n" : "PASS\n", bad);
    return bad != 0;
}

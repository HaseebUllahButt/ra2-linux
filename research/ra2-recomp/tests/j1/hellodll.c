#include <windows.h>
#include <stdio.h>
__declspec(dllexport) int twice(int x){ printf("dll OK\n"); fflush(stdout); return 2*x; }
BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID p){ return TRUE; }

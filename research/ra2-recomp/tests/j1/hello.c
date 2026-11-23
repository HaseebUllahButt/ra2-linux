#include <stdio.h>
#include <windows.h>
typedef int (*fn)(int);
int main(void){
  HMODULE h = LoadLibraryA("hellodll.dll");
  if(!h){printf("FAIL load\n");return 1;}
  fn f=(fn)GetProcAddress(h,"twice");
  long double x = 1.0L/3.0L;
  printf("exe OK, dll says %d, ld=%d\n", f?f(21):-1, (int)sizeof x);
  return 0;
}

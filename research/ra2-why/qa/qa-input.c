// Fake human input on an X display through XTest.
// Usage: qa-input move X Y | click [left|right|middle] X Y | dclick X Y |
//        drag X1 Y1 X2 Y2 | key NAME | keydown NAME | keyup NAME | hold NAME MS
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static Display* d;
static void sync_wait(int ms){XFlush(d);XSync(d,False);usleep(ms*1000);}
static void move(int x,int y){XTestFakeMotionEvent(d,-1,x,y,0);sync_wait(30);}
static int btn(const char* s){return !strcmp(s,"right")?3:!strcmp(s,"middle")?2:1;}
static void press(int b,int down){XTestFakeButtonEvent(d,b,down,0);sync_wait(40);}
static KeyCode code(const char* name){KeySym k=XStringToKeysym(name);if(k==NoSymbol){fprintf(stderr,"bad key %s\n",name);exit(2);}return XKeysymToKeycode(d,k);}
int main(int c,char** v){
  if(c<2){fprintf(stderr,"see source for usage\n");return 2;}
  d=XOpenDisplay(NULL);if(!d){fprintf(stderr,"no display\n");return 1;}
  const char* cmd=v[1];
  if(!strcmp(cmd,"move")&&c==4) move(atoi(v[2]),atoi(v[3]));
  else if(!strcmp(cmd,"click")&&c==5){move(atoi(v[3]),atoi(v[4]));int b=btn(v[2]);press(b,1);press(b,0);}
  else if(!strcmp(cmd,"dclick")&&c==4){move(atoi(v[2]),atoi(v[3]));press(1,1);press(1,0);usleep(60000);press(1,1);press(1,0);}
  else if(!strcmp(cmd,"drag")&&c==6){int x1=atoi(v[2]),y1=atoi(v[3]),x2=atoi(v[4]),y2=atoi(v[5]);move(x1,y1);press(1,1);
    for(int i=1;i<=10;i++)move(x1+(x2-x1)*i/10,y1+(y2-y1)*i/10);press(1,0);}
  else if(!strcmp(cmd,"key")&&c==3){KeyCode k=code(v[2]);XTestFakeKeyEvent(d,k,True,0);sync_wait(40);XTestFakeKeyEvent(d,k,False,0);sync_wait(40);}
  else if(!strcmp(cmd,"keydown")&&c==3){XTestFakeKeyEvent(d,code(v[2]),True,0);sync_wait(40);}
  else if(!strcmp(cmd,"keyup")&&c==3){XTestFakeKeyEvent(d,code(v[2]),False,0);sync_wait(40);}
  else if(!strcmp(cmd,"hold")&&c==4){KeyCode k=code(v[2]);XTestFakeKeyEvent(d,k,True,0);sync_wait(atoi(v[3]));XTestFakeKeyEvent(d,k,False,0);sync_wait(40);}
  else {fprintf(stderr,"bad command\n");return 2;}
  XCloseDisplay(d);return 0;
}

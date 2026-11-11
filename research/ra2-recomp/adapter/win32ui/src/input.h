/* input.h: raw input events from the platform layer (SDL). */
#ifndef RA2UI_INPUT_H
#define RA2UI_INPUT_H
#include "ui.h"
enum { UI_RAW_MOVE = 1, UI_RAW_BUTTON, UI_RAW_KEY, UI_RAW_WHEEL };
typedef struct {
    int kind;
    int x, y;            /* screen coordinates */
    int button, down;    /* 1 left, 2 right, 3 middle */
    int vk, scan, ext, repeat, wheel;
    DWORD time;
} ui_raw;
void ui_raw_push(const ui_raw *r);
UINT ui_vk_to_scan(UINT vk);
UINT ui_scan_to_vk(UINT scan);
const char *ui_scan_name(int scan, int ext);
#endif

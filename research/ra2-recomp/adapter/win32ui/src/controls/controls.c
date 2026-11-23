/* Built-in control registration. Control state uses private storage. */
#include "compat.h"
/* ================= Progress, hotkey, list view (first versions) ================= */
static LRESULT UIAPI listview_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg >= 0x1000 && msg < 0x1100) { ui_log("listview: message %04x not supported yet (stage 2)", msg); return 0; }
    if (msg == WM_PAINT) { PAINTSTRUCT ps; if (!wp) { ui_BeginPaint(h, &ps); ui_EndPaint(h, &ps); } return 0; }
    return ui_DefWindowProcA(h, msg, wp, lp);
}
static LRESULT UIAPI scrollbar_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg >= 0xe0 && msg <= 0xef) { ui_log("scrollbar: message %04x not supported yet", msg); return 0; }
    return ui_DefWindowProcA(h, msg, wp, lp);
}

void ui_controls_free(ui_win *w)
{
    if (!w->ctl) return;
    if (w->cls && w->cls->proc == listbox_proc) {
        ctl_listbox_free(w);
    }
    free(w->ctl); w->ctl = NULL;
}
void ui_controls_init(void)
{
    static int done;
    if (done) return;
    done = 1;
    HCURSOR arrow = 0x00b17f00;
    ui_register_builtin("Button", button_proc, 0x0b /* CS_VREDRAW|CS_HREDRAW|CS_DBLCLKS */ | 0x4000, 0, arrow);
    ui_register_builtin("Static", static_proc, 0x4000 | 8, 0, arrow);
    ui_register_builtin("ListBox", listbox_proc, 0x4008, 0, arrow);
    ui_register_builtin("ComboLBox", listbox_proc, 0x0808 | 0x20000 /* CS_DROPSHADOW-ish */, 0, arrow);
    ui_register_builtin("ComboBox", combo_proc, 0x4008 | 3, 0, arrow);
    ui_register_builtin("Edit", edit_proc, 0x4008, 0, 0x00b17f01);
    ui_register_builtin("ScrollBar", scrollbar_proc, 0x4008 | 3, 0, arrow);
    ui_register_builtin("#32770", ui_DefDlgProcA, 0x0808, DLGWINDOWEXTRA, arrow);
    ui_register_builtin("msctls_trackbar32", trackbar_proc, 3 | 8, 0, arrow);
    ui_register_builtin("msctls_progress32", progress_proc, 3, 0, arrow);
    ui_register_builtin("msctls_hotkey32", hotkey_proc, 3, 0, arrow);
    ui_register_builtin("SysListView32", listview_proc, 3 | 8, 0, arrow);
}

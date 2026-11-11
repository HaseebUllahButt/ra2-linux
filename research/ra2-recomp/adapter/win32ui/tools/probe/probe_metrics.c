/* probe_metrics.exe: print GetSystemMetrics, GetSysColor and AdjustWindowRectEx results under Wine. */
#include <windows.h>
#include <stdio.h>
int main(void)
{
    for (int i = 0; i <= 99; i++) printf("SM %d %d\n", i, GetSystemMetrics(i));
    for (int i = 0x1000; i <= 0x1003; i++) printf("SM %d %d\n", i, GetSystemMetrics(i));
    for (int i = 0; i <= 30; i++) printf("COLOR %d %06lx\n", i, GetSysColor(i));
    DWORD styles[] = { 0, WS_POPUP, WS_CHILD, WS_CAPTION, WS_POPUP | WS_CAPTION, WS_POPUP | WS_CAPTION | WS_SYSMENU,
                       WS_POPUP | WS_BORDER, WS_POPUP | WS_DLGFRAME, WS_THICKFRAME, WS_OVERLAPPEDWINDOW, WS_CHILD | WS_BORDER,
                       0x80c800c4, 0x84c800c4, 0x80c000c4, 0x90c800c0 };
    DWORD exs[] = { 0, WS_EX_DLGMODALFRAME, WS_EX_CLIENTEDGE, WS_EX_STATICEDGE, WS_EX_WINDOWEDGE, WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
                    WS_EX_TOOLWINDOW, WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT };
    for (unsigned s = 0; s < sizeof styles / sizeof *styles; s++)
        for (unsigned e = 0; e < sizeof exs / sizeof *exs; e++) {
            RECT r = { 100, 100, 200, 200 };
            AdjustWindowRectEx(&r, styles[s], FALSE, exs[e]);
            printf("ADJ %08lx %08lx %ld %ld %ld %ld\n", styles[s], exs[e], 100 - r.left, 100 - r.top, r.right - 200, r.bottom - 200);
        }
    /* real windows: style after creation and client size */
    for (unsigned s = 0; s < sizeof styles / sizeof *styles; s++)
        for (unsigned e = 0; e < 3; e++) {
            if (styles[s] & WS_CHILD) continue;
            HWND h = CreateWindowExA(exs[e], "Static", "x", styles[s], 10, 10, 300, 200, 0, 0, 0, 0);
            RECT cr; GetClientRect(h, &cr);
            printf("WIN %08lx %08lx -> style %08lx ex %08lx client %ldx%ld\n", styles[s], exs[e], GetWindowLongA(h, GWL_STYLE), GetWindowLongA(h, GWL_EXSTYLE), cr.right, cr.bottom);
            DestroyWindow(h);
        }
    TEXTMETRICA tm; HDC dc = GetDC(0); GetTextMetricsA(dc, &tm);
#define TMALL(n) printf("TMALL %s %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %d %d %d %d %d %d %d %d %d\n", n, tm.tmHeight, tm.tmAscent, tm.tmDescent, tm.tmInternalLeading, tm.tmExternalLeading, tm.tmAveCharWidth, tm.tmMaxCharWidth, tm.tmWeight, tm.tmOverhang, tm.tmDigitizedAspectX, tm.tmDigitizedAspectY, tm.tmFirstChar, tm.tmLastChar, tm.tmDefaultChar, tm.tmBreakChar, tm.tmItalic, tm.tmUnderlined, tm.tmStruckOut, tm.tmPitchAndFamily, tm.tmCharSet)
    TMALL("system");
    int stock[] = { 10, 11, 12, 13, 14, 16, 17 };
    for (int i = 0; i < 7; i++) { SelectObject(dc, GetStockObject(stock[i])); GetTextMetricsA(dc, &tm); char n[16]; sprintf(n, "stock%d", stock[i]); TMALL(n); }
    int hs[] = { -11, -12, -13, -14, -16, -8, -9, -10, 11, 12, 13, 14, 16 };
    const char *faces[] = { "MS Sans Serif", "Arial", "Tahoma", "Courier New", "" };
    for (int fi = 0; fi < 5; fi++) for (int hi = 0; hi < 13; hi++) for (int wt = 400; wt <= 700; wt += 300) {
        HFONT ff = CreateFontA(hs[hi], 0, 0, 0, wt, 0, 0, 0, DEFAULT_CHARSET, 0, 0, PROOF_QUALITY, 0, faces[fi]);
        SelectObject(dc, ff); GetTextMetricsA(dc, &tm); char n[64]; sprintf(n, "font|%s|%d|%d", faces[fi], hs[hi], wt); TMALL(n);
        SIZE sz; GetTextExtentPoint32A(dc, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", 52, &sz); printf("EXT %s %ld %ld\n", n, sz.cx, sz.cy);
    }
    SelectObject(dc, GetStockObject(13)); GetTextMetricsA(dc, &tm);
    printf("TM sys height %ld ascent %ld descent %ld internal %ld external %ld avew %ld maxw %ld weight %ld\n", tm.tmHeight, tm.tmAscent, tm.tmDescent, tm.tmInternalLeading, tm.tmExternalLeading, tm.tmAveCharWidth, tm.tmMaxCharWidth, tm.tmWeight);
    HFONT f = CreateFontA(-11, 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET, 0, 0, PROOF_QUALITY, 0, "MS Sans Serif");
    SelectObject(dc, f); GetTextMetricsA(dc, &tm);
    printf("TM mssans8 height %ld ascent %ld descent %ld internal %ld external %ld avew %ld maxw %ld weight %ld\n", tm.tmHeight, tm.tmAscent, tm.tmDescent, tm.tmInternalLeading, tm.tmExternalLeading, tm.tmAveCharWidth, tm.tmMaxCharWidth, tm.tmWeight);
    printf("DBU %08lx\n", GetDialogBaseUnits());
    for (int i = 0; i < 120; i += 2) printf("CAPS %d %d\n", i, GetDeviceCaps(dc, i));
    return 0;
}

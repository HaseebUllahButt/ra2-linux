/* Test driver. Read the game's patched imports, so this works with native or
   portable dialogs. Send the same standard button notification in both runs. */
static BOOL (WINAPI *ui_peek)(MSG *,HWND,UINT,UINT,UINT);
static BOOL (WINAPI *ui_enum)(HWND,WNDENUMPROC,LPARAM);
static BOOL (WINAPI *ui_visible)(HWND);
static int (WINAPI *ui_text)(HWND,char *,int);
static int (WINAPI *ui_class)(HWND,char *,int);
static int (WINAPI *ui_id)(HWND);
static HWND (WINAPI *ui_parent)(HWND);
static BOOL (WINAPI *ui_post)(HWND,UINT,WPARAM,LPARAM);
static unsigned menu_step;
static DWORD menu_at;
static HWND menu_button;
static BOOL CALLBACK menu_find(HWND hwnd,LPARAM parameter)
{
    const char *wanted=(const char *)parameter;
    char cls[64],title[128];
    if (!ui_visible(hwnd)) return TRUE;
    ui_class(hwnd,cls,sizeof(cls)); ui_text(hwnd,title,sizeof(title));
    if (!_stricmp(cls,"Button")) {
        if (!_stricmp(title,wanted)) { menu_button=hwnd; return FALSE; }
        if (!_stricmp(title,"OK") || !_stricmp(title,"GUI:OK") || !_stricmp(title,"TXT_OK")) {
            ui_post(ui_parent(hwnd),WM_COMMAND,MAKEWPARAM(ui_id(hwnd),BN_CLICKED),(LPARAM)hwnd);
            trace("auto dismissed startup notice");
        }
    }
    return TRUE;
}
static BOOL WINAPI menu_peek(MSG *message,HWND hwnd,UINT first,UINT last,UINT flags)
{
    BOOL result=ui_peek(message,hwnd,first,last,flags);
    static const char *steps[]={"GUI:SinglePlayer","GUI:Skirmish","GUI:StartGame"};
    DWORD now=GetTickCount();
    if (menu_step<3 && now-menu_at>=1000) {
        menu_at=now; menu_button=NULL;
        ui_enum(NULL,menu_find,(LPARAM)steps[menu_step]);
        if (menu_button) {
            char line[160]; snprintf(line,sizeof(line),"auto button %s",steps[menu_step]); trace(line);
            ui_post(ui_parent(menu_button),WM_COMMAND,MAKEWPARAM(ui_id(menu_button),BN_CLICKED),(LPARAM)menu_button);
            menu_step++;
        }
    }
    return result;
}
static void **game_import(const char *wanted)
{
    BYTE *base=(BYTE *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *pe=(IMAGE_NT_HEADERS *)(base+((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_IMPORT_DESCRIPTOR *dll=(IMAGE_IMPORT_DESCRIPTOR *)(base+pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for (;dll->Name;dll++) {
        IMAGE_THUNK_DATA *names=(IMAGE_THUNK_DATA *)(base+(dll->OriginalFirstThunk?dll->OriginalFirstThunk:dll->FirstThunk));
        void **iat=(void **)(base+dll->FirstThunk);
        for (unsigned i=0;names[i].u1.AddressOfData;i++) {
            if (IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *name=(IMAGE_IMPORT_BY_NAME *)(base+names[i].u1.AddressOfData);
            if (!strcmp((char *)name->Name,wanted)) return iat+i;
        }
    }
    stop("menu driver import missing"); return NULL;
}
static void menu_install(void)
{
#define UI(name,ptr) ptr=(void *)*game_import(name)
    UI("EnumChildWindows",ui_enum); UI("IsWindowVisible",ui_visible);
    UI("GetWindowTextA",ui_text); UI("GetClassNameA",ui_class);
    UI("GetDlgCtrlID",ui_id); UI("GetParent",ui_parent); UI("PostMessageA",ui_post);
    UI("PeekMessageA",ui_peek);
#undef UI
    patch(game_import("PeekMessageA"),menu_peek);
}

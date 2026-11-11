#!/usr/bin/env python3
"""Generate typed import wrappers in record.c from ui_api.inc."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
p=root/'src/win/record.c'
s=p.read_text().split('/* GENERATED WRAPPERS */')[0]+'/* GENERATED WRAPPERS */\n'
entries=[]
for dll,ret,name,args in re.findall(r'UIFN\((\w+),\s*(\w+),\s*(\w+),\s*\(([^)]*)\)\)',(root/'src/ui_api.inc').read_text()):
    parsed=[]
    if args!='void':
        for arg in args.split(','):
            typ,key=re.match(r'(.+?)(\w+)$',arg.strip()).groups();parsed.append((typ.strip(),key))
    entries.append((dll,name))
    s+=f'static {ret} UIAPI rec_{name}({args}) {{\n'
    s+=f' DWORD entry_error=GetLastError(); rec_begin("{name}",__builtin_return_address(0));\n'
    for typ,key in parsed:
        contents= not ((typ=='char *') or (name=='PeekMessageA' and key=='m') or (name in ('GetWindowRect','GetClientRect','GetUpdateRect','GetCursorPos','GetKeyNameTextA','GetWindowTextA','GetClassNameA','SetRect') and '*' in typ) or (name=='IntersectRect' and key=='d'))
        s+=f' rec_arg("{typ}","{key}",(uintptr_t){key},{int(contents)});\n'
    if any(k=='m' and t=='UINT' for t,k in parsed):s+=' rec_message(h,m,w,l);\n'
    s+=' rec_line();\n'
    if name=='RegisterClassA':s+=' WNDCLASSA copy; if(c){copy=*c;copy.lpfnWndProc=(WNDPROC)rec_wrap((uintptr_t)c->lpfnWndProc,0);c=&copy;}\n'
    if name in ['SetWindowLongA']:s+=' if(i==-4||i==4)v=(LONG)rec_wrap((uintptr_t)v,0);\n'
    if 'Dialog' in name and any(k=='f' for t,k in parsed):s+=' f=(DLGPROC)rec_wrap((uintptr_t)f,0);\n'
    if name=='SetTimer':s+=' f=(TIMERPROC)rec_wrap((uintptr_t)f,1);\n'
    if name=='CallWindowProcA':s+=' p=(WNDPROC)rec_wrap((uintptr_t)p,0);\n'
    s+=f' static {ret}(UIAPI *real)({args}); if(rec_wine&&!real)real=rec_real("{dll}.DLL","{name}");\n'
    call=f'(rec_wine?real:ui_{name})('+','.join(k for t,k in parsed)+')'
    s+=' SetLastError(entry_error);\n'
    s+= (' '+call+';\n' if ret=='void' else f' {ret} result={call};\n')
    s+=' DWORD exit_error=GetLastError();\n'
    if name in ['SetWindowLongA','GetWindowLongA']:s+=' if(i==-4||i==4) result=(LONG)rec_unwrap((uintptr_t)result);\n'
    s+=f' rec_end("{name}","{ret}",'+('0' if ret=='void' else '(uintptr_t)result')+');\n'
    for typ,key in parsed:
        if '*' in typ and not typ.startswith('const'):
            if name=='PeekMessageA':s+=f' if(result)rec_arg("{typ}","{key}",(uintptr_t){key},1);\n'
            else:s+=f' rec_arg("{typ}","{key}",(uintptr_t){key},1);\n'
        if typ=='char *':s+=f' rec_add(" {key}_text=");rec_string((uintptr_t){key});\n'
    s+=' rec_line();SetLastError(exit_error);'+('return result;' if ret!='void' else '')+'\n}\n'
s+='''static INT UICDECL rec_wsprintfA(char *out,const char *fmt,...) {
 DWORD entry_error=GetLastError();va_list ap;va_start(ap,fmt);rec_begin("wsprintfA",__builtin_return_address(0));rec_arg("const char *","fmt",(uintptr_t)fmt,1);rec_line();
 rec_arg("void *","out",(uintptr_t)out,0);
 va_list copy;va_copy(copy,ap);unsigned index=0;
 for(const char *q=fmt;*q;q++){if(*q!='%')continue;q++;if(*q=='%')continue;while(*q&&strchr("-+#0. 123456789hlwI",*q))q++;if(!*q)break;unsigned value=va_arg(copy,unsigned);rec_add(" arg%u=",index++);if(*q=='s'||*q=='S')rec_string(value);else rec_value("UINT",value);}
 va_end(copy);rec_line();SetLastError(entry_error);
 INT result;if(rec_wine){INT(UIAPI *f)(char*,const char*,va_list)=rec_real("USER32.DLL","wvsprintfA");result=f(out,fmt,ap);}else result=ui_wvsprintfA(out,fmt,ap);
 DWORD exit_error=GetLastError();va_end(ap);rec_end("wsprintfA","INT",result);rec_arg("const char *","out",(uintptr_t)out,1);rec_line();SetLastError(exit_error);return result;
}
static void *rec_import(const char *dll,const char *name,void *fallback) {
 if(!rec_file)return rec_wine?rec_real(dll,name):fallback;
'''
for dll,name in entries:s+=f' if(!strcmp(dll,"{dll}.DLL")&&!strcmp(name,"{name}"))return rec_{name};\n'
s+=''' if(!strcmp(name,"wsprintfA"))return rec_wsprintfA;
 if(!strcmp(dll,"COMCTL32.DLL")&&!strcmp(name,"#17"))return rec_InitCommonControls;
 ui_fatal("recorder has no wrapper for %s!%s",dll,name);return 0;
}
static void rec_init(void) {
 const char *path=getenv("RA2UI_RECORD");const char *mode=getenv("RA2UI_MODE");rec_wine=mode&&!strcmp(mode,"wine");
 if(path&&*path){rec_file=fopen(path,"wb");if(!rec_file)ui_fatal("cannot open RA2UI_RECORD: %s",path);setvbuf(rec_file,0,_IONBF,0);}
}
'''
p.write_text(s)
(root/"src/win/dllmain.c").touch()

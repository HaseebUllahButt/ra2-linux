/* Reuse the lead's tested entry loader. Keep the test game's log handles. */
#include <windows.h>
static BOOL capture_create_process(LPCSTR application,LPSTR command,LPSECURITY_ATTRIBUTES process_attributes,
    LPSECURITY_ATTRIBUTES thread_attributes,BOOL inherit,DWORD flags,LPVOID environment,LPCSTR directory,
    LPSTARTUPINFOA startup,LPPROCESS_INFORMATION process)
{
    (void)inherit;
    STARTUPINFOA with_logs=*startup;
    with_logs.dwFlags|=STARTF_USESTDHANDLES;
    with_logs.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
    with_logs.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);
    with_logs.hStdError=GetStdHandle(STD_ERROR_HANDLE);
    HANDLE handles[]={with_logs.hStdInput,with_logs.hStdOutput,with_logs.hStdError};
    for (unsigned i=0;i<3;i++) if (handles[i] && handles[i]!=INVALID_HANDLE_VALUE)
        SetHandleInformation(handles[i],HANDLE_FLAG_INHERIT,HANDLE_FLAG_INHERIT);
    return CreateProcessA(application,command,process_attributes,thread_attributes,TRUE,flags,environment,directory,&with_logs,process);
}
#define CreateProcessA capture_create_process
#include "../../shim/launch.c"

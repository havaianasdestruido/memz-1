/* Minimal stand-in for <Psapi.h>, see Windows.h in this directory. */
#ifndef _WINSTUB_PSAPI_H
#define _WINSTUB_PSAPI_H

#include <Windows.h>

DWORD GetProcessImageFileNameA(HANDLE process, LPSTR out, DWORD size);

#endif

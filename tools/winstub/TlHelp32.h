/* Minimal stand-in for <TlHelp32.h>, see Windows.h in this directory. */
#ifndef _WINSTUB_TLHELP32_H
#define _WINSTUB_TLHELP32_H

#include <Windows.h>

#define TH32CS_SNAPPROCESS 0x00000002u

typedef struct
{
    DWORD dwSize;
    DWORD cntUsage;
    DWORD th32ProcessID;
    void *th32DefaultHeapID;
    DWORD th32ModuleID;
    DWORD cntThreads;
    DWORD th32ParentProcessID;
    LONG  pcPriClassBase;
    DWORD dwFlags;
    CHAR  szExeFile[MAX_PATH];
} PROCESSENTRY32;

HANDLE CreateToolhelp32Snapshot(DWORD flags, DWORD process_id);
BOOL   Process32First(HANDLE snapshot, PROCESSENTRY32 *entry);
BOOL   Process32Next(HANDLE snapshot, PROCESSENTRY32 *entry);

#endif

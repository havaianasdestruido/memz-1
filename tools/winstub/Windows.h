/*
 * Minimal stand-in for <Windows.h>.
 *
 * This project cannot be cross compiled here, so every source file is also
 * compiled against these declarations on the build host:
 *
 *   make -C tools/bench syntax
 *
 * The point is to type check the Win32 branches of the sources -- code that
 * the host benchmark in tools/bench never reaches. The signatures mirror the
 * real headers; nothing links against this and it is not an emulator.
 */

#ifndef _WINSTUB_WINDOWS_H
#define _WINSTUB_WINDOWS_H

#include <stddef.h>
#include <string.h>

/* ---- base types ---------------------------------------------------- */

typedef int             BOOL;
typedef unsigned char   BYTE;
typedef unsigned short  WORD;
typedef unsigned int    DWORD;
typedef unsigned long   ULONG_PTR;
typedef long            LONG;
typedef unsigned long   DWORD_PTR;
typedef int             INT;
typedef unsigned int    UINT;
typedef void           *HANDLE;
typedef void           *HWND;
typedef void           *HDC;
typedef void           *HICON;
typedef void           *HCURSOR;
typedef void           *HBRUSH;
typedef void           *HMODULE;
typedef void           *HINSTANCE;
typedef void           *HKEY;
typedef HKEY           *PHKEY;
typedef void           *HHOOK;
typedef void           *HWAVEOUT;
typedef void           *LPVOID;
typedef unsigned long   HCRYPTPROV;
typedef BYTE           *LPBYTE;
typedef DWORD          *LPDWORD;
typedef char            CHAR;
typedef char           *LPSTR;
typedef const char     *LPCSTR;
typedef wchar_t         WCHAR;
typedef WCHAR          *LPWSTR;
typedef const WCHAR    *LPCWSTR;
typedef void           *FARPROC;
typedef unsigned char   BOOLEAN;
typedef unsigned long   ULONG;
typedef void           *HWINSTA;

typedef ULONG_PTR  WPARAM;
typedef long       LPARAM;
typedef long       LRESULT;

#define WINAPI
#define CALLBACK
#define APIENTRY

typedef DWORD (WINAPI *LPTHREAD_START_ROUTINE)(void *parameter);

#define TRUE  1
#define FALSE 0

/* ---- limits and handles -------------------------------------------- */

#define MAX_PATH             260
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG)-1)
#define INVALID_FILE_SIZE    ((DWORD)0xFFFFFFFF)
#define ERROR_SUCCESS        0L

#define GENERIC_READ          0x80000000u
#define GENERIC_WRITE         0x40000000u
#define FILE_SHARE_READ       0x00000001u
#define FILE_SHARE_WRITE      0x00000002u
#define CREATE_ALWAYS         2u
#define OPEN_EXISTING         3u
#define FILE_ATTRIBUTE_NORMAL 0x00000080u

#define KEY_READ              0x20019u
#define HKEY_CURRENT_USER     ((HKEY)(ULONG_PTR)0x80000001)

#define PROCESS_QUERY_INFORMATION 0x0400u
#define TOKEN_QUERY               0x0008u
#define TOKEN_ADJUST_PRIVILEGES   0x0020u

#define LMEM_ZEROINIT 0x0040u
#define GMEM_ZEROINIT 0x0040u

/* ---- metrics, icons, window styles --------------------------------- */

#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SM_CXICON   11
#define SM_CYICON   12

#define IDI_ERROR    32513
#define IDI_WARNING  32515

#define WS_DLGFRAME  0x00400000u
#define WS_POPUP     0x80000000u

#define INPUT_KEYBOARD    1u
#define INPUT_MOUSE       0u
#define INPUT_HARDWARE    2u

#define SMTO_NORMAL       0x0000u
#define SMTO_BLOCK        0x0001u
#define SMTO_ABORTIFHUNG  0x0002u

#define WM_CLOSE          0x0010u
#define WM_ENDSESSION     0x0016u
#define WM_GETTEXT        0x000Du
#define WM_SETTEXT        0x000Cu
#define WM_DESTROY        0x0002u

#define MB_OK             0x00000000u
#define MB_ICONHAND       0x00000010u
#define MB_ICONWARNING    0x00000030u
#define MB_SYSTEMMODAL    0x00001000u

#define SW_SHOWDEFAULT    10
#define SEE_MASK_NOCLOSEPROCESS 0x00000040u
#define HIGH_PRIORITY_CLASS     0x00000080u

#define WH_CBT          5
#define HCBT_CREATEWND  3

/* ---- raster ops ----------------------------------------------------- */

#define SRCCOPY    0x00CC0020u
#define NOTSRCCOPY 0x00330008u

/* ---- structures ----------------------------------------------------- */

typedef struct { LONG x; LONG y; } POINT;
typedef struct { LONG left; LONG top; LONG right; LONG bottom; } RECT;

typedef struct
{
    DWORD  cbSize;
    UINT   style;
    LRESULT (CALLBACK *lpfnWndProc)(HWND, UINT, WPARAM, LPARAM);
    int    cbClsExtra;
    int    cbWndExtra;
    HINSTANCE hInstance;
    HICON  hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCSTR lpszMenuName;
    LPCSTR lpszClassName;
    HICON  hIconSm;
} WNDCLASSEXA;

typedef struct { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam;
                 DWORD time; POINT pt; } MSG;

typedef struct
{
    LPVOID lpCreateParams;
    HINSTANCE hInstance;
    void  *hMenu;
    HWND  hwndParent;
    int   cy, cx, y, x;
    LONG  style;
    LPCSTR lpszName;
    LPCSTR lpszClass;
    DWORD dwExStyle;
} CREATESTRUCT;

typedef struct { CREATESTRUCT *lpcs; HWND hwndInsertAfter; } CBT_CREATEWND;

typedef struct
{
    WORD  wVk;
    WORD  wScan;
    DWORD dwFlags;
    DWORD time;
    ULONG_PTR dwExtraInfo;
} KEYBDINPUT;

typedef struct { DWORD type; KEYBDINPUT ki; } INPUT;

typedef struct
{
    WORD  wFormatTag;
    WORD  nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD  nBlockAlign;
    WORD  wBitsPerSample;
    WORD  cbSize;
} WAVEFORMATEX;

typedef struct
{
    LPSTR  lpData;
    DWORD  dwBufferLength;
    DWORD  dwBytesRecorded;
    DWORD_PTR dwUser;
    DWORD  dwFlags;
    DWORD  dwLoops;
    struct wavehdr_tag *lpNext;
    DWORD_PTR reserved;
} WAVEHDR;

typedef struct
{
    DWORD cbSize;
    ULONG fMask;
    HWND  hwnd;
    LPCSTR lpVerb;
    LPCSTR lpFile;
    LPCSTR lpParameters;
    LPCSTR lpDirectory;
    int   nShow;
    HINSTANCE hInstApp;
    LPVOID lpIDList;
    LPCSTR lpClass;
    HKEY  hkeyClass;
    DWORD dwHotKey;
    HANDLE hIcon;
    HANDLE hProcess;
} SHELLEXECUTEINFO;

/* ---- synchronisation ------------------------------------------------ */

typedef struct { int dummy; } CRITICAL_SECTION;

void InitializeCriticalSection(CRITICAL_SECTION *cs);
void EnterCriticalSection(CRITICAL_SECTION *cs);
void LeaveCriticalSection(CRITICAL_SECTION *cs);
void DeleteCriticalSection(CRITICAL_SECTION *cs);

LONG InterlockedIncrement(LONG volatile *target);
LONG InterlockedExchange(LONG volatile *target, LONG value);
LONG InterlockedCompareExchange(LONG volatile *target, LONG exchange,
                                LONG comparand);

/* ---- kernel32 -------------------------------------------------------- */

void   Sleep(DWORD ms);
void   ExitProcess(UINT code);
HANDLE GetCurrentProcess(void);
DWORD  GetCurrentThreadId(void);
HANDLE CreateThread(void *attributes, DWORD stack,
                    DWORD (WINAPI *start)(void *), void *parameter,
                    DWORD flags, LPDWORD thread_id);
HANDLE LocalAlloc(UINT flags, DWORD bytes);
void  *LocalFree(void *block);
void  *GlobalAlloc(UINT flags, DWORD bytes);
void  *GlobalFree(void *block);
BOOL   CloseHandle(HANDLE object);
HANDLE OpenProcess(DWORD access, BOOL inherit, DWORD pid);
HMODULE LoadLibraryA(LPCSTR name);
FARPROC GetProcAddress(HMODULE module, LPCSTR name);
DWORD  GetModuleFileName(HMODULE module, LPSTR buffer, DWORD size);
HMODULE GetModuleHandle(LPCSTR name);
BOOL   SetPriorityClass(HANDLE process, DWORD priority_class);

HANDLE CreateFileA(LPCSTR path, DWORD access, DWORD share, void *security,
                   DWORD disposition, DWORD flags, HANDLE template_file);
BOOL   ReadFile(HANDLE file, void *buffer, DWORD bytes, DWORD *read,
                void *overlapped);
BOOL   WriteFile(HANDLE file, const void *buffer, DWORD bytes, DWORD *written,
                 void *overlapped);
DWORD  GetFileSize(HANDLE file, DWORD *high);

LONG   RegOpenKeyExA(HKEY key, LPCSTR sub, DWORD options,
                     DWORD access, PHKEY result);
LONG   RegQueryValueExA(HKEY key, LPCSTR name, LPDWORD reserved,
                        LPDWORD type, LPBYTE data, LPDWORD size);
LONG   RegCloseKey(HKEY key);
DWORD  ExpandEnvironmentStringsA(LPCSTR src, LPSTR dst, DWORD size);

int    lstrcmpA(LPCSTR a, LPCSTR b);
int    lstrlenW(LPCWSTR str);

/* ---- user32 / gdi32 -------------------------------------------------- */

int    GetSystemMetrics(int index);
HICON  LoadIconA(HINSTANCE instance, LPCSTR id);
void   OutputDebugStringA(LPCSTR text);

HWND   GetDesktopWindow(void);
HDC    GetWindowDC(HWND window);
BOOL   GetWindowRect(HWND window, RECT *rect);
BOOL   GetCursorPos(POINT *point);
BOOL   SetCursorPos(int x, int y);
BOOL   EnumChildWindows(HWND parent, BOOL (CALLBACK *proc)(HWND, LPARAM),
                        LPARAM param);
LRESULT SendMessageTimeoutW(HWND window, UINT msg, WPARAM wParam,
                            LPARAM lParam, UINT flags, UINT timeout,
                            DWORD_PTR *result);
UINT   SendInput(UINT count, INPUT *inputs, int size);
BOOL   DrawIcon(HDC dc, int x, int y, HICON icon);
BOOL   BitBlt(HDC dst, int x, int y, int w, int h, HDC src,
              int sx, int sy, DWORD op);
BOOL   StretchBlt(HDC dst, int x, int y, int w, int h, HDC src,
                  int sx, int sy, int sw, int sh, DWORD op);

HHOOK  SetWindowsHookEx(int id, LRESULT (CALLBACK *proc)(int, WPARAM, LPARAM),
                        HINSTANCE module, DWORD thread);
BOOL   UnhookWindowsHookEx(HHOOK hook);
LRESULT CallNextHookEx(HHOOK hook, int code, WPARAM wParam, LPARAM lParam);

int    MessageBoxA(HWND owner, LPCSTR text, LPCSTR caption, UINT type);
int    MessageBoxW(HWND owner, LPCWSTR text, LPCWSTR caption, UINT type);

HANDLE ShellExecuteA(HWND owner, LPCSTR verb, LPCSTR file, LPCSTR parameters,
                     LPCSTR directory, int show);
HANDLE ShellExecute(HWND owner, LPCSTR verb, LPCSTR file, LPCSTR parameters,
                    LPCSTR directory, int show);
BOOL   ShellExecuteEx(SHELLEXECUTEINFO *info);

short  RegisterClassExA(const WNDCLASSEXA *cls);
HWND   CreateWindowExA(DWORD ex_style, LPCSTR class_name, LPCSTR window_name,
                       DWORD style, int x, int y, int w, int h, HWND parent,
                       void *menu, HINSTANCE instance, void *param);
LRESULT DefWindowProc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL   GetMessage(MSG *msg, HWND window, UINT min, UINT max);
BOOL   TranslateMessage(const MSG *msg);
LRESULT DispatchMessage(const MSG *msg);

/* ---- winmm ------------------------------------------------------------ */

#define WAVE_FORMAT_PCM 1u
#define WAVE_MAPPER     ((DWORD)-1)
#define CALLBACK_NULL   0x00000000u
#define WHDR_DONE       0x00000001u
#define SND_SYNC        0x00000000u
#define SND_MEMORY      0x00000004u
#define SND_RESOURCE    0x00040004u

DWORD waveOutOpen(HWAVEOUT *out, DWORD device, const WAVEFORMATEX *format,
                  DWORD_PTR callback, DWORD_PTR instance, DWORD flags);
DWORD waveOutPrepareHeader(HWAVEOUT out, WAVEHDR *header, DWORD size);
DWORD waveOutWrite(HWAVEOUT out, WAVEHDR *header, DWORD size);
DWORD waveOutUnprepareHeader(HWAVEOUT out, WAVEHDR *header, DWORD size);
BOOL  PlaySoundA(LPCSTR sound, HMODULE module, DWORD flags);

#endif /* _WINSTUB_WINDOWS_H */

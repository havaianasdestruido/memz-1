/*
 * Minimal stand-in for <Wincrypt.h>, see Windows.h in the same directory.
 * Only what src/cache.c uses to refill the random number pool.
 */
#ifndef _WINSTUB_WINCRYPT_H
#define _WINSTUB_WINCRYPT_H

#include <Windows.h>

#define PROV_RSA_FULL        1u
#define CRYPT_SILENT         0x40u
#define CRYPT_VERIFYCONTEXT  0xF0000000u

BOOL CryptAcquireContext(HCRYPTPROV *prov, LPCSTR container, LPCSTR provider,
                         DWORD type, DWORD flags);
BOOL CryptGenRandom(HCRYPTPROV prov, DWORD length, BYTE *buffer);

#endif /* _WINSTUB_WINCRYPT_H */

; Forwarding stubs: each export bumps its call counter, then tail-jumps to the
; real function in g_real. Signature-agnostic: no registers except flags change.
; Index order must match proxy.cpp.
EXTERN g_real:QWORD
EXTERN g_calls:QWORD

STUB MACRO name, index
name PROC
    lock inc QWORD PTR [g_calls + index * 8]
    jmp QWORD PTR [g_real + index * 8]
name ENDP
ENDM

.code
STUB Proxy_GetFileVersionInfoA, 0
STUB Proxy_GetFileVersionInfoByHandle, 1
STUB Proxy_GetFileVersionInfoExA, 2
STUB Proxy_GetFileVersionInfoExW, 3
STUB Proxy_GetFileVersionInfoSizeA, 4
STUB Proxy_GetFileVersionInfoSizeExA, 5
STUB Proxy_GetFileVersionInfoSizeExW, 6
STUB Proxy_GetFileVersionInfoSizeW, 7
STUB Proxy_GetFileVersionInfoW, 8
STUB Proxy_VerFindFileA, 9
STUB Proxy_VerFindFileW, 10
STUB Proxy_VerInstallFileA, 11
STUB Proxy_VerInstallFileW, 12
STUB Proxy_VerLanguageNameA, 13
STUB Proxy_VerLanguageNameW, 14
STUB Proxy_VerQueryValueA, 15
STUB Proxy_VerQueryValueW, 16
STUB Proxy_DirectInput8Create, 17
STUB Proxy_DllCanUnloadNow, 18
STUB Proxy_DllGetClassObject, 19
STUB Proxy_DllRegisterServer, 20
STUB Proxy_DllUnregisterServer, 21
STUB Proxy_GetdfDIJoystick, 22
STUB Proxy_getConfiguration, 23
STUB Proxy_getConfigurationSize, 24
STUB Proxy_getSessionConfigurationSize, 25
STUB Proxy_getVersion, 26
STUB Proxy_setConfiguration, 27
STUB Proxy_setStopSessionCallback, 28
STUB Proxy_setUpdateCameraFunc, 29
STUB Proxy_stopSession, 30
STUB Proxy_updateCamera, 31
END

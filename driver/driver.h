#pragma once

/* kernel-mode header — компилируется только на Windows с WDK + MSVC */
#if defined(_WIN32) && defined(_MSC_VER)

#include <ntddk.h>
#include <ntstrsafe.h>
#include "shared.h"

/* Pool tag 'IxP\0' */
#define IX_POOL_TAG 'xIxP'

/* FileDispositionInformationEx (Win10 2004+) — define our own names to avoid
 * redefinition conflicts with newer WDK headers. */
#define IX_FILE_DISP_EX_CLASS    ((FILE_INFORMATION_CLASS)64)
#define IX_DISP_DELETE           0x00000001ul
#define IX_DISP_POSIX_SEMANTICS  0x00000002ul

typedef struct { ULONG Flags; } IX_FILE_DISP_EX_INFO;

DRIVER_INITIALIZE  DriverEntry;
DRIVER_UNLOAD      IxDriverUnload;

_Dispatch_type_(IRP_MJ_CREATE)
_Dispatch_type_(IRP_MJ_CLOSE)
DRIVER_DISPATCH    IxDispatchCreateClose;

_Dispatch_type_(IRP_MJ_DEVICE_CONTROL)
DRIVER_DISPATCH    IxDispatchIoctl;

NTSTATUS IxDeleteFile(PIX_DELETE_REQUEST request);
NTSTATUS IxKillProcess(PIX_KILL_REQUEST request);
NTSTATUS IxSetAttributes(PIX_SET_ATTR_REQUEST request);

#endif /* _WIN32 && _MSC_VER */

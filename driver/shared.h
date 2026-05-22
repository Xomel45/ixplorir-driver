#pragma once

#ifdef _WIN32
#include <windows.h>
#else
typedef unsigned short WCHAR;
typedef unsigned long  ULONG;
#define CTL_CODE(DeviceType, Function, Method, Access) \
    (((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))
#define FILE_DEVICE_UNKNOWN 0x00000022
#define METHOD_BUFFERED     0
#define FILE_ANY_ACCESS     0
#endif

#define IXPLORIR_DEVICE_NAME    L"\\Device\\IxplorirDevice"
#define IXPLORIR_DEVICE_LINK    L"\\DosDevices\\IxplorirDevice"
#define IXPLORIR_DEVICE_USER    L"\\\\.\\IxplorirDevice"

#define IXPLORIR_IOCTL_BASE     0x800

#define IOCTL_IX_DELETE_FILE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, IXPLORIR_IOCTL_BASE + 0, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_IX_KILL_PROCESS \
    CTL_CODE(FILE_DEVICE_UNKNOWN, IXPLORIR_IOCTL_BASE + 1, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_IX_SET_ATTRIBUTES \
    CTL_CODE(FILE_DEVICE_UNKNOWN, IXPLORIR_IOCTL_BASE + 2, METHOD_BUFFERED, FILE_ANY_ACCESS)

#pragma pack(push, 1)

typedef struct {
    WCHAR path[32767];
} IX_DELETE_REQUEST;

typedef struct {
    ULONG pid;
} IX_KILL_REQUEST;

typedef struct {
    WCHAR path[32767];
    ULONG attributes;
} IX_SET_ATTR_REQUEST;

#pragma pack(pop)

#ifdef _WIN32
typedef IX_DELETE_REQUEST   *PIX_DELETE_REQUEST;
typedef IX_KILL_REQUEST     *PIX_KILL_REQUEST;
typedef IX_SET_ATTR_REQUEST *PIX_SET_ATTR_REQUEST;
#endif

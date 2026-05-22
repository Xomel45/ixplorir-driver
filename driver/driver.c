#include "driver.h"

#if defined(_WIN32) && defined(_MSC_VER)

static PDEVICE_OBJECT g_DeviceObject = NULL;

/* ── private helpers ────────────────────────────────────────────────────── */

/*
 * Allocates an NT-namespace path buffer ("\??\" + win32Path).
 * Caller must ExFreePoolWithTag(*outBuf, IX_POOL_TAG) on success.
 */
static NTSTATUS
IxpBuildNtPath(const WCHAR *win32Path, UNICODE_STRING *ustr, WCHAR **outBuf)
{
    static const WCHAR kPfx[]  = L"\\??\\";
    static const ULONG kPfxCch = 4;

    SIZE_T   srcCch = 0;
    NTSTATUS st = RtlStringCchLengthW(win32Path, 32767, &srcCch);
    if (!NT_SUCCESS(st) || srcCch == 0) return STATUS_INVALID_PARAMETER;

    SIZE_T totalCch   = kPfxCch + srcCch;
    SIZE_T totalBytes = (totalCch + 1) * sizeof(WCHAR);

    WCHAR *buf = ExAllocatePool2(POOL_FLAG_PAGED, totalBytes, IX_POOL_TAG);
    if (!buf) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(buf,            kPfx,     kPfxCch  * sizeof(WCHAR));
    RtlCopyMemory(buf + kPfxCch,  win32Path, srcCch  * sizeof(WCHAR));
    buf[totalCch] = L'\0';

    ustr->Buffer        = buf;
    ustr->Length        = (USHORT)(totalCch   * sizeof(WCHAR));
    ustr->MaximumLength = (USHORT)(totalBytes);

    *outBuf = buf;
    return STATUS_SUCCESS;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    UNICODE_STRING deviceName = RTL_CONSTANT_STRING(IXPLORIR_DEVICE_NAME);
    UNICODE_STRING deviceLink = RTL_CONSTANT_STRING(IXPLORIR_DEVICE_LINK);

    NTSTATUS status = IoCreateDevice(
        DriverObject,
        0,
        &deviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &g_DeviceObject
    );

    if (!NT_SUCCESS(status))
        return status;

    status = IoCreateSymbolicLink(&deviceLink, &deviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(g_DeviceObject);
        return status;
    }

    DriverObject->DriverUnload                         = IxDriverUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE]         = IxDispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = IxDispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = IxDispatchIoctl;

    return STATUS_SUCCESS;
}

void IxDriverUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    UNICODE_STRING deviceLink = RTL_CONSTANT_STRING(IXPLORIR_DEVICE_LINK);
    IoDeleteSymbolicLink(&deviceLink);
    IoDeleteDevice(g_DeviceObject);
}

NTSTATUS IxDispatchCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status      = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS IxDispatchIoctl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    PIO_STACK_LOCATION stack  = IoGetCurrentIrpStackLocation(Irp);
    ULONG              code   = stack->Parameters.DeviceIoControl.IoControlCode;
    PVOID              buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG              inLen  = stack->Parameters.DeviceIoControl.InputBufferLength;
    NTSTATUS           status = STATUS_INVALID_DEVICE_REQUEST;

    switch (code) {
        case IOCTL_IX_DELETE_FILE:
            if (inLen >= sizeof(IX_DELETE_REQUEST))
                status = IxDeleteFile((PIX_DELETE_REQUEST)buffer);
            else
                status = STATUS_BUFFER_TOO_SMALL;
            break;

        case IOCTL_IX_KILL_PROCESS:
            if (inLen >= sizeof(IX_KILL_REQUEST))
                status = IxKillProcess((PIX_KILL_REQUEST)buffer);
            else
                status = STATUS_BUFFER_TOO_SMALL;
            break;

        case IOCTL_IX_SET_ATTRIBUTES:
            if (inLen >= sizeof(IX_SET_ATTR_REQUEST))
                status = IxSetAttributes((PIX_SET_ATTR_REQUEST)buffer);
            else
                status = STATUS_BUFFER_TOO_SMALL;
            break;
    }

    Irp->IoStatus.Status      = status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

/* ── IxDeleteFile ───────────────────────────────────────────────────────── */

NTSTATUS IxDeleteFile(PIX_DELETE_REQUEST request)
{
    NTSTATUS          status;
    UNICODE_STRING    uPath;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK   iosb;
    HANDLE            hFile   = NULL;
    WCHAR            *pathBuf = NULL;

    status = IxpBuildNtPath(request->path, &uPath, &pathBuf);
    if (!NT_SUCCESS(status)) return status;

    InitializeObjectAttributes(&oa, &uPath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwCreateFile(
        &hFile,
        FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | DELETE | SYNCHRONIZE,
        &oa, &iosb, NULL,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
        NULL, 0
    );
    if (!NT_SUCCESS(status)) goto cleanup;

    /* Clear read-only / system / hidden so the FS allows deletion */
    {
        FILE_BASIC_INFORMATION bi;
        RtlZeroMemory(&bi, sizeof(bi));
        bi.FileAttributes = FILE_ATTRIBUTE_NORMAL;
        ZwSetInformationFile(hFile, &iosb, &bi, sizeof(bi), FileBasicInformation);
    }

    /* POSIX-semantics delete (Win10 2004+): succeeds even with open handles */
    {
        IX_FILE_DISP_EX_INFO diEx;
        diEx.Flags = IX_DISP_DELETE | IX_DISP_POSIX_SEMANTICS;
        status = ZwSetInformationFile(hFile, &iosb,
                                      &diEx, sizeof(diEx),
                                      IX_FILE_DISP_EX_CLASS);
    }

    /* Fallback: classic disposition (may fail if a handle lacks FILE_SHARE_DELETE) */
    if (!NT_SUCCESS(status)) {
        FILE_DISPOSITION_INFORMATION di = { TRUE };
        status = ZwSetInformationFile(hFile, &iosb,
                                      &di, sizeof(di),
                                      FileDispositionInformation);
    }

cleanup:
    if (hFile)   ZwClose(hFile);
    if (pathBuf) ExFreePoolWithTag(pathBuf, IX_POOL_TAG);
    return status;
}

/* ── IxKillProcess ──────────────────────────────────────────────────────── */

NTSTATUS IxKillProcess(PIX_KILL_REQUEST request)
{
    PEPROCESS eprocess = NULL;
    HANDLE    hProc    = NULL;
    NTSTATUS  status;

    /* Protect system/idle processes (PID 0 and 4) */
    if (request->pid <= 4) return STATUS_ACCESS_DENIED;

    status = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)request->pid, &eprocess);
    if (!NT_SUCCESS(status)) return status;

    status = ObOpenObjectByPointer(
        eprocess,
        OBJ_KERNEL_HANDLE,
        NULL,
        PROCESS_TERMINATE,
        *PsProcessType,
        KernelMode,
        &hProc
    );
    if (!NT_SUCCESS(status)) goto cleanup;

    status = ZwTerminateProcess(hProc, 0);
    ZwClose(hProc);

cleanup:
    ObDereferenceObject(eprocess);
    return status;
}

/* ── IxSetAttributes ────────────────────────────────────────────────────── */

NTSTATUS IxSetAttributes(PIX_SET_ATTR_REQUEST request)
{
    NTSTATUS          status;
    UNICODE_STRING    uPath;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK   iosb;
    HANDLE            hFile   = NULL;
    WCHAR            *pathBuf = NULL;

    status = IxpBuildNtPath(request->path, &uPath, &pathBuf);
    if (!NT_SUCCESS(status)) return status;

    InitializeObjectAttributes(&oa, &uPath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwCreateFile(
        &hFile,
        FILE_WRITE_ATTRIBUTES | SYNCHRONIZE,
        &oa, &iosb, NULL,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT,
        NULL, 0
    );
    if (!NT_SUCCESS(status)) goto cleanup;

    {
        FILE_BASIC_INFORMATION bi;
        RtlZeroMemory(&bi, sizeof(bi));
        bi.FileAttributes = request->attributes ? request->attributes
                                                 : FILE_ATTRIBUTE_NORMAL;
        status = ZwSetInformationFile(hFile, &iosb, &bi, sizeof(bi), FileBasicInformation);
    }

cleanup:
    if (hFile)   ZwClose(hFile);
    if (pathBuf) ExFreePoolWithTag(pathBuf, IX_POOL_TAG);
    return status;
}

#endif /* _WIN32 && _MSC_VER */

#include <windows.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <stdio.h>

#define DRIVER_NAME      L"IxplorirDriver"
#define DRIVER_DISPLAY   L"Ixplorir Driver"
#define DRIVER_SYS_NAME  L"ixplorir-driver.sys"
#define DRIVER_CER_NAME  L"ixplorir-driver.cer"

#define GITHUB_HOST      L"github.com"
#define GITHUB_SYS_PATH  L"/Xomel45/ixplorir-driver/releases/latest/download/ixplorir-driver.sys"
#define GITHUB_CER_PATH  L"/Xomel45/ixplorir-driver/releases/latest/download/ixplorir-driver.cer"

/* ------------------------------------------------------------------ */
/*  Утилиты                                                             */
/* ------------------------------------------------------------------ */

static void print_error(const wchar_t* action, DWORD code)
{
    wchar_t msg[256] = {0};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, NULL, code, 0, msg, 256, NULL);
    fwprintf(stderr, L"[!] %ls failed (0x%08X): %ls\n", action, code, msg);
}

static wchar_t* get_system32_path(const wchar_t* filename)
{
    static wchar_t path[MAX_PATH];
    GetSystemDirectoryW(path, MAX_PATH);
    wcsncat_s(path, MAX_PATH, L"\\drivers\\", _TRUNCATE);
    wcsncat_s(path, MAX_PATH, filename, _TRUNCATE);
    return path;
}

static wchar_t* get_temp_path(const wchar_t* filename)
{
    static wchar_t path[MAX_PATH];
    GetTempPathW(MAX_PATH, path);
    wcsncat_s(path, MAX_PATH, filename, _TRUNCATE);
    return path;
}

/* ------------------------------------------------------------------ */
/*  Скачивание файла через WinHTTP                                      */
/* ------------------------------------------------------------------ */

static int download_file(const wchar_t* host, const wchar_t* url_path,
                         const wchar_t* dest_path)
{
    HINTERNET session = WinHttpOpen(L"ixplorir-installer/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { print_error(L"WinHttpOpen", GetLastError()); return 1; }

    HINTERNET connect = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) { print_error(L"WinHttpConnect", GetLastError()); goto err_session; }

    HINTERNET request = WinHttpOpenRequest(connect, L"GET", url_path,
                                           NULL, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!request) { print_error(L"WinHttpOpenRequest", GetLastError()); goto err_connect; }

    /* Следуем редиректам GitHub */
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        print_error(L"WinHttpSendRequest", GetLastError());
        goto err_request;
    }

    if (!WinHttpReceiveResponse(request, NULL)) {
        print_error(L"WinHttpReceiveResponse", GetLastError());
        goto err_request;
    }

    HANDLE file = CreateFileW(dest_path, GENERIC_WRITE, 0, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        print_error(L"CreateFile (dest)", GetLastError());
        goto err_request;
    }

    BYTE   buf[8192];
    DWORD  read = 0;
    while (WinHttpReadData(request, buf, sizeof(buf), &read) && read > 0) {
        DWORD written = 0;
        WriteFile(file, buf, read, &written, NULL);
    }

    CloseHandle(file);
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return 0;

err_request:  WinHttpCloseHandle(request);
err_connect:  WinHttpCloseHandle(connect);
err_session:  WinHttpCloseHandle(session);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Сертификат: добавить публичный .cer в cert store                   */
/* ------------------------------------------------------------------ */

static int install_certificate(const wchar_t* cer_path)
{
    HANDLE file = CreateFileW(cer_path, GENERIC_READ, FILE_SHARE_READ,
                              NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        print_error(L"CreateFile (cer)", GetLastError());
        return 1;
    }

    DWORD  size = GetFileSize(file, NULL);
    BYTE*  data = HeapAlloc(GetProcessHeap(), 0, size);
    DWORD  read = 0;
    ReadFile(file, data, size, &read, NULL);
    CloseHandle(file);

    const wchar_t* stores[] = { L"ROOT", L"TrustedPublisher" };
    for (int i = 0; i < 2; i++) {
        HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W,
                                         X509_ASN_ENCODING,
                                         0,
                                         CERT_SYSTEM_STORE_LOCAL_MACHINE,
                                         stores[i]);
        if (!store) {
            print_error(L"CertOpenStore", GetLastError());
            HeapFree(GetProcessHeap(), 0, data);
            return 1;
        }

        CertAddEncodedCertificateToStore(store,
                                         X509_ASN_ENCODING,
                                         data, size,
                                         CERT_STORE_ADD_REPLACE_EXISTING,
                                         NULL);
        CertCloseStore(store, 0);
    }

    HeapFree(GetProcessHeap(), 0, data);
    wprintf(L"[+] Certificate installed.\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test signing                                                        */
/* ------------------------------------------------------------------ */

static int enable_test_signing(void)
{
    STARTUPINFOW        si = { .cb = sizeof(si) };
    PROCESS_INFORMATION pi = {0};

    BOOL ok = CreateProcessW(NULL,
                             L"bcdedit /set testsigning on",
                             NULL, NULL, FALSE,
                             CREATE_NO_WINDOW,
                             NULL, NULL, &si, &pi);
    if (!ok) {
        print_error(L"bcdedit", GetLastError());
        return 1;
    }

    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    wprintf(L"[+] Test signing enabled (reboot required).\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/*  SCM: установка / удаление                                          */
/* ------------------------------------------------------------------ */

static int scm_install(const wchar_t* sys_path)
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (!scm) { print_error(L"OpenSCManager", GetLastError()); return 1; }

    SC_HANDLE svc = CreateServiceW(scm, DRIVER_NAME, DRIVER_DISPLAY,
                                   SERVICE_ALL_ACCESS,
                                   SERVICE_KERNEL_DRIVER,
                                   SERVICE_SYSTEM_START,
                                   SERVICE_ERROR_NORMAL,
                                   sys_path,
                                   NULL, NULL, NULL, NULL, NULL);
    if (!svc) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_EXISTS)
            fwprintf(stderr, L"[!] Driver already installed.\n");
        else
            print_error(L"CreateService", err);
        CloseServiceHandle(scm);
        return 1;
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return 0;
}

static int scm_uninstall(void)
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) { print_error(L"OpenSCManager", GetLastError()); return 1; }

    SC_HANDLE svc = OpenServiceW(scm, DRIVER_NAME, SERVICE_STOP | DELETE);
    if (!svc) { print_error(L"OpenService", GetLastError()); CloseServiceHandle(scm); return 1; }

    SERVICE_STATUS status;
    ControlService(svc, SERVICE_CONTROL_STOP, &status);
    if (!DeleteService(svc))
        print_error(L"DeleteService", GetLastError());
    else
        wprintf(L"[+] Driver removed.\n");

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Полная установка                                                    */
/* ------------------------------------------------------------------ */

static int do_install(void)
{
    wprintf(L"[*] Downloading driver...\n");
    wchar_t* sys_tmp = get_temp_path(DRIVER_SYS_NAME);
    wchar_t* cer_tmp = get_temp_path(DRIVER_CER_NAME);

    if (download_file(GITHUB_HOST, GITHUB_SYS_PATH, sys_tmp)) return 1;
    if (download_file(GITHUB_HOST, GITHUB_CER_PATH, cer_tmp)) return 1;
    wprintf(L"[+] Downloaded.\n");

    wprintf(L"[*] Installing certificate...\n");
    if (install_certificate(cer_tmp)) return 1;

    wprintf(L"[*] Enabling test signing...\n");
    if (enable_test_signing()) return 1;

    wprintf(L"[*] Copying driver to System32\\drivers...\n");
    wchar_t* sys_dest = get_system32_path(DRIVER_SYS_NAME);
    if (!CopyFileW(sys_tmp, sys_dest, FALSE)) {
        print_error(L"CopyFile", GetLastError());
        return 1;
    }
    DeleteFileW(sys_tmp);
    DeleteFileW(cer_tmp);

    wprintf(L"[*] Registering service...\n");
    if (scm_install(sys_dest)) return 1;

    wprintf(L"[+] Done. Please reboot for test signing to take effect.\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Точка входа                                                         */
/* ------------------------------------------------------------------ */

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 2) {
        fwprintf(stderr, L"Usage:\n"
                         L"  installer.exe install\n"
                         L"  installer.exe uninstall\n");
        return 1;
    }

    if (_wcsicmp(argv[1], L"install") == 0)   return do_install();
    if (_wcsicmp(argv[1], L"uninstall") == 0) return scm_uninstall();

    fwprintf(stderr, L"[!] Unknown command: %ls\n", argv[1]);
    return 1;
}

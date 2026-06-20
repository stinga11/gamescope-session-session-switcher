# session-switcher.c

A high-performance, native C reimplementation of `steamos-session-select` powered by `libsystemd`.

[![Language](https://shields.io)](https://wikipedia.org)
[![Platform](https://shields.io)](https://wikipedia.org)
[![Init](https://shields.io)](https://systemd.io)

## 🚀 Overview

`session-switcher.c` replaces the shell-based orchestration layer of `steamos-session-select`. Instead of repeatedly chaining expensive `fork`/`exec` calls to `qdbus` and `loginctl`, it uses direct D-Bus communication via `libsystemd` for performance-critical operations.

This implementation has been fully validated with extensive **Gamescope ↔ Plasma** round-trips on **CachyOS** (`plasmalogin`).

## 🛠️ Key Fixes & Debugging Insights

Developing this utility required resolving several low-level session management issues:

* **Plasmalogin Caching Bug:** Fixed by explicitly restarting the service to force a re-read of `Session=`, as a standard logout is insufficient.
* **VT Release Race Condition:** Mitigated by implementing a precise *settle delay* during Virtual Terminal handovers between sessions.
* **Hanging `WAYLAND_DISPLAY`:** Addressed via `gamescope-session.service` configuration (handled externally, outside this binary).

## ⚠️ Scope & Limitations

This tool is a drop-in optimization for the orchestration layer only. **It does not replace:**
* `steam-set-session`
* Native systemd unit-level fixes and workarounds.

## 📦 Building and Installation

### Prerequisites

Ensure you have the development files for `libsystemd` installed on your system (e.g., `systemd-devel` on Arch/CachyOS or `libsystemd-dev` on Debian/Ubuntu).

### Compilation

All build instructions and flags are configured inside the provided compilation script:

```bash
chmod +x build.sh
./build.sh
```

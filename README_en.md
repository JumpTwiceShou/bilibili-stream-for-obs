# OBS Bilibili Streaming Plugin

This is an OBS Studio plugin for Bilibili live streaming. It supports QR-code login, reading and updating live-room information, starting a Bilibili live session, and retrieving the RTMP server and stream key for OBS.

The first public release of this repository is `1.0.0`. The basic workflow follows [Zarosmm/obs-bilibili-stream](https://github.com/Zarosmm/obs-bilibili-stream), with additional behavior inspired by [lanyangyin/OBSscripts-bilibili-live](https://github.com/lanyangyin/OBSscripts-bilibili-live).

## Changes in 1.2.0

- Fixed the face-verification QR window repeatedly closing and reopening when the verification-status and start-live responses disagree.
- The plugin now keeps one face-verification window open until start-live succeeds or a terminal error occurs.

## Changes in 1.1.0

- Starting a live session reads the current Bilibili room category instead of overwriting it with a hard-coded default.
- The room-info dialog preloads the current title and category.
- The room-info dialog uses one `确认修改` button to submit both title and category updates.
- When OBS native streaming stops, the plugin only checks and stops a still-managed Bilibili session that this plugin started. It does not stop sessions started by another device or workflow.
- When Bilibili requires face verification, the plugin shows the QR code, polls the verification status, and automatically continues starting the live session after verification succeeds.
- Login, start/stop, room updates, and verification polling run in the background without blocking the OBS UI thread.

## Installation

### Windows

1. Download `bilibili-stream-for-obs-1.2.0-windows-x64.zip` from this repository's [Releases page](https://github.com/JumpTwiceShou/bilibili-stream-for-obs/releases).
2. Extract the archive.
3. Move the extracted `bilibili-stream-for-obs` folder into:

   ```text
   C:\ProgramData\obs-studio\plugins
   ```

4. Verify the structure:

   ```text
   C:\ProgramData\obs-studio\plugins\
   └── bilibili-stream-for-obs\
       ├── bin\
       │   └── 64bit\
       │       └── bilibili-stream-for-obs.dll
       └── data\
           └── locale\
               └── (locale .ini files)
   ```

5. Restart OBS Studio. The plugin is loaded when the `Bilibili直播` menu appears.

## Usage

1. Open `Bilibili直播` -> `登录` and scan the QR code with the Bilibili mobile app.
2. To sign out, open `Bilibili直播` -> `登录` -> `退出登录`. This only removes credentials saved by this plugin and does not sign out other devices. Stop any plugin-managed live session first.
3. Open `Bilibili直播` -> `更新直播间信息` to edit the current room title and category.
4. Open `Bilibili直播` -> `开始直播` to start the Bilibili live session and copy the RTMP server/key into OBS custom stream settings.
5. Click OBS native `停止直播` to stop streaming. The plugin will automatically synchronize the stop-live request to Bilibili. The plugin menu still provides a manual stop option if synchronization fails.

## Face Verification

If Bilibili requires face verification, scan the QR code shown by the plugin. After verification succeeds, the plugin detects the verification status and continues starting the live session automatically. The QR window closes only after start-live succeeds; if Bilibili still requires verification, the same window remains open and continues waiting.

This verification-status check uses a non-public Bilibili endpoint and may stop working if the platform changes its APIs.

## Notes

- This plugin depends on Bilibili live APIs, which may change without notice.
- Plugin state is stored in the current user's OBS configuration directory. On Windows, login cookies are encrypted with user-bound DPAPI. macOS/Linux currently use an owner-only compatibility store, so use the plugin only on trusted machines.
- Login cookies, CSRF values, and RTMP stream keys are not written to ordinary logs; CSRF values and stream keys are not persisted. Still inspect configuration and logs before sharing them.
- Follow Bilibili platform rules and applicable laws.

OBS logs:

```text
Windows: C:\Users\<YourUser>\AppData\Roaming\obs-studio\logs
macOS: ~/Library/Logs/obs-studio/
Linux: ~/.config/obs-studio/logs/
```

## Build

Windows build requirements:

- OBS Studio 30.0 or later.
- Visual Studio Build Tools with the C++ desktop toolchain.
- CMake.

Example:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --parallel
cmake --install build_x64 --prefix release/RelWithDebInfo --config RelWithDebInfo
```

Windows validation including offline regression tests:

```powershell
cmake --preset windows-ci-x64
cmake --build --preset windows-ci-x64 --parallel
ctest --preset windows-ci-x64
```

## License

This repository is released under the GNU General Public License v3.0. See [LICENSE](LICENSE).

Third-party components keep their original licenses:

- `src/json11`: Copyright (c) 2013 Dropbox, Inc., MIT License.
- `src/qrcodegen`: Copyright (c) Project Nayuki, MIT License.

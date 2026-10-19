# OBS Bilibili 直播插件

这是一个为 OBS Studio 开发的 Bilibili 开播辅助插件。插件可以在 OBS 菜单中完成扫码登录、读取和更新直播间信息、开启 Bilibili 直播，并获取 RTMP 推流地址和推流码。

本仓库公开发布版本从 `1.0.0` 开始。使用方式整体参考 [Zarosmm/obs-bilibili-stream](https://github.com/Zarosmm/obs-bilibili-stream)，并融合了 [lanyangyin/OBSscripts-bilibili-live](https://github.com/lanyangyin/OBSscripts-bilibili-live) 中与 OBS 前端事件同步相关的思路。

## 1.3.0 更新

- 新增 OBS 开播、停播联动独立开关，支持滑动切换和自动保存。
- 优化人脸识别等待状态，识别成功后自动继续开播，减少重复尝试和提示反复切换。
- 适配新版扫码登录；登录成功但未开通直播间或房间信息获取失败时，提供明确提示。
- 切换账号或登录失效后清理旧账号的房间信息和推流凭据，保留同步开关等通用设置；同一账号重新登录保留线路偏好。
- 直播间修改会等待接口结果，失败时保留输入，支持重试。
- 新增推流线路连接延迟推荐、地址复制和一键替换 OBS 推流配置；升级时保留用户设置。
- 启动日志增加版本和构建标识，方便定位不同测试包；清理旧版推流信息窗口。
- 改进网络请求取消与退出稳定性。
- 对于某些情况下禁止 OBS 开播的问题，给出了一个神秘解决方法。

## 1.2.0 更新

- 修复人脸验证状态与开播结果不一致时二维码窗口反复关闭、重新弹出的问题。
- 插件现在会保留同一个人脸验证窗口，只有开播成功或出现终止错误时才结束等待。

## 1.1.0 更新

- 开播时自动读取当前直播间分区，不再默认把直播间分区覆盖为“英雄联盟”。
- “更新直播间信息”窗口会读取当前直播间标题和分区，并显示到输入框和分区选择框中。
- “更新直播间信息”使用一个“确认修改”按钮提交变更，并在窗口内显示标题和分区的结果；失败时保留输入，重试只补交未成功的修改，完成后可单独关闭窗口。
- 点击 OBS 原生“停止直播”后，插件只会检测并同步关闭由本插件启动、仍处于托管状态的 B 站直播间，不会误关其他设备或流程发起的直播。
- 遇到 Bilibili 人脸识别时，扫码完成后插件会检测认证状态，并自动继续开播。
- 登录、开停播、直播间更新和认证轮询都在后台执行，不阻塞 OBS 界面线程。

## 安装方法

### Windows

1. 从本仓库 [Releases 页面](https://github.com/JumpTwiceShou/bilibili-stream-for-obs/releases) 下载 `bilibili-stream-for-obs-1.3.0-windows-x64.zip`。
2. 解压压缩包。
3. 将解压后的 `bilibili-stream-for-obs` 文件夹放到 OBS 插件目录：

   ```text
   C:\ProgramData\obs-studio\plugins
   ```

4. 确认目录结构如下：

   ```text
   C:\ProgramData\obs-studio\plugins\
   └── bilibili-stream-for-obs\
       ├── bin\
       │   └── 64bit\
       │       └── bilibili-stream-for-obs.dll
       └── data\
           └── locale\
               └── (相关 .ini 语言文件)
   ```

5. 重启 OBS Studio，菜单栏中出现“Bilibili直播”即代表插件已加载。

## 使用方法

### 1. 登录 Bilibili

1. 打开 OBS Studio。
2. 在菜单栏选择“Bilibili直播” -> “登录”。
3. 使用手机 Bilibili App 扫描二维码。
4. 登录成功后，菜单中的登录状态会显示为“已登录”。
5. 需要退出时，选择“Bilibili直播” -> “登录” -> “退出登录”；该操作只清除本插件保存的本地凭据，不会让其他设备退出登录。插件托管直播期间需先停播。

### 2. 更新直播间信息

1. 在菜单栏选择“Bilibili直播” -> “更新直播间信息”。
2. 插件会先读取当前直播间标题和分区，并显示到窗口中。
3. 修改标题、主分区或子分区。
4. 点击“确认修改”，插件会一次提交标题和分区变更。

如果标题或分区没有变化，插件会提示“直播间信息没有变化”。

### 3. 开始直播

1. 登录并确认直播间分区后，点击 OBS 主界面的“开始直播”。
2. 在“OBS 联动设置”中开启开播联动时，插件会先启动 Bilibili 直播；需要人脸验证时等待验证完成，然后自动继续 OBS 推流。
3. 从“Bilibili直播” -> “推流地址与线路”可查看本次返回的地址和 OBS 内置线路，点击线路即可复制地址；停止 OBS 推流后可一键替换当前推流配置，也可单独复制推流码。
4. 也可以通过插件菜单“开始直播”先准备 Bilibili 直播，再选择线路并点击 OBS“开始直播”。
5. “OBS 联动设置”中的开播、关播开关独立保存，用户配置在后续正常更新插件文件时保留。

### 4. 结束直播

推荐直接点击 OBS 右下角“停止直播”。插件检测到 OBS 推流停止后，会自动检查 Bilibili 直播状态，并同步关闭 B 站直播间。

如果自动同步失败，仍可以在菜单栏选择“Bilibili直播” -> “停止直播”手动关闭 Bilibili 直播间。

## 人脸识别说明

如果 Bilibili 开播接口要求人脸识别，插件会显示人脸识别二维码。请使用手机 Bilibili App 扫码完成认证。

认证完成后，插件会轮询人脸识别状态接口并自动再次调用开播接口。只有开播接口确认成功后二维码窗口才会关闭；如果平台仍要求验证，插件会保留同一个窗口继续等待，不需要手动重新点击“开始直播”。

注意：人脸识别状态检测使用的是 Bilibili 非官方公开接口，可能会因为平台接口调整而失效。

## 注意事项

- 本插件依赖 Bilibili 直播相关接口，接口变更可能导致登录、开播、停播、更新直播间信息或人脸识别检测失效。
- 插件把状态保存到当前用户的 OBS 配置目录；Windows 使用 DPAPI 绑定当前用户加密登录 Cookie。macOS/Linux 暂时使用仅当前用户可读写的兼容存储，因此仍应只在可信电脑上使用。
- 插件不会把登录 Cookie、CSRF 或 RTMP 推流码写入普通日志，不持久化 CSRF；最近的推流地址、推流码和线路选择使用与登录信息相同的保护方式保存到用户配置目录；但分享日志和配置前仍建议检查内容。
- 请遵守 Bilibili 平台规则和相关法律法规，使用本插件产生的后果由使用者自行承担。
- 遇到问题时请先查看 OBS 日志。

OBS 日志位置：

```text
Windows: C:\Users\<YourUser>\AppData\Roaming\obs-studio\logs
macOS: ~/Library/Logs/obs-studio/
Linux: ~/.config/obs-studio/logs/
```

## 构建说明

Windows 构建需要：

- OBS Studio 30.0 或更高版本。
- Visual Studio Build Tools，包含 C++ 桌面开发工具链。
- CMake。

本项目沿用 OBS 插件模板的 CMake 构建流程，OBS、Qt 和预编译依赖由构建配置下载。公开发布包以 GitHub Release 附件为准。

本地 Windows 构建示例：

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --parallel
cmake --install build_x64 --prefix release/RelWithDebInfo --config RelWithDebInfo
```

包含离线回归测试的 Windows 验证：

```powershell
cmake --preset windows-ci-x64
cmake --build --preset windows-ci-x64 --parallel
ctest --preset windows-ci-x64
```

## 致谢与参考

- [Zarosmm/obs-bilibili-stream](https://github.com/Zarosmm/obs-bilibili-stream)：原生 OBS 插件主体和使用方式参考。
- [lanyangyin/OBSscripts-bilibili-live](https://github.com/lanyangyin/OBSscripts-bilibili-live)：OBS Python 脚本功能参考，尤其是 OBS 前端事件与停播同步思路。
- [SocialSisterYi/bilibili-API-collect](https://github.com/SocialSisterYi/bilibili-API-collect)：Bilibili API 资料参考。

## 许可证

本仓库按 GNU General Public License v3.0 发布，详见 [LICENSE](LICENSE)。

第三方组件保持其原许可证：

- `src/json11`：Copyright (c) 2013 Dropbox, Inc.，MIT License。
- `src/qrcodegen`：Copyright (c) Project Nayuki，MIT License。

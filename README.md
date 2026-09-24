# Deskflow FileCopy

基于 [Deskflow](https://github.com/deskflow/deskflow) 二次开发，在键鼠共享和原有剪贴板功能的基础上，增加 **Windows ↔ Windows 文件复制**。

本项目是独立维护的非官方衍生项目，不由 Deskflow 上游团队发布或背书。文件复制扩展目前仅支持 Windows；上游的跨平台支持不等于本扩展支持跨平台文件互传。

## 文件复制

```text
A 电脑选中文件或文件夹 → Ctrl+C
                 ↓
B 电脑接收到本地缓存 → 提示“文件就绪，可以粘贴”
                 ↓
B 电脑打开目标文件夹 → Ctrl+V
```

反向操作相同。支持普通文件、多选、文件夹、空目录和中文文件名。传输完成前请等待，不要提前粘贴。主窗口显示银灰色进度条、已传/总大小、文件数量、传输速度和预计剩余时间，并提供取消按钮；校验完成后才会发布接收文件到剪贴板。

速度根据最近约 3 秒的字节变化估算，剩余时间会随网络和磁盘状态变化。准备、校验及等待对端确认时不显示速度与剩余时间；约 2 秒没有字节推进时也暂停显示估算值。进度只有成功完成后达到 100%。文件数量包含普通文件和空文件，不包含目录；发送端显示已完成/总数，接收中显示已接收数量，完成后显示总数。

### 开始使用

1. 两端使用本项目的同一版本，先按 Deskflow 原有方式配置服务端、客户端和屏幕位置。
2. 两端启用 TLS 加密，并核对、信任对端指纹；服务端配置中启用剪贴板共享。
3. 两端打开“首选项 → 常规”，勾选“启用文件复制”。此功能默认关闭。
4. 使用已登录 Windows 用户的 **Desktop（桌面）模式**运行。以 SYSTEM 身份或在会话 0 运行时不启用文件复制。
5. 复制文件，等待接收端提示就绪，再到目标文件夹粘贴。

不要把上游 Deskflow 的下载包当作本扩展的另一端。与未启用扩展的对端连接时不发送文件扩展数据；这不代表已经验证所有旧版本、其他衍生项目和网络配置的兼容性。

## 下载

在[本仓库 Releases](https://github.com/EugeneEvan/deskflow-filecopy/releases) 选择 Windows x64 的 `*-setup.exe` 安装程序或 `*-portable.zip` 便携包，文件名、校验值和变更说明以该版本实际附件为准。

当前 FileCopy 版本为 `0.2.0`，对应标签 `filecopy-v0.2.0`；下载以 Releases 实际附件为准。应用程序名称、底层协议和配置标识仍沿用 Deskflow；FileCopy 发行版本与“关于”界面的上游版本号分别记录。

安装程序使用独立的 Deskflow FileCopy 产品标识，可选择安装路径；请使用独立目录，避免覆盖原版 Deskflow。安装时可勾选“登录后启动”（默认不勾选），仅为当前用户创建启动快捷方式，不安装系统服务。升级保留这一选择；重新运行安装器取消勾选可关闭，卸载时移除本产品的启动快捷方式。

首次请完成双方配置并成功连接。应用正常退出时会记住核心的启动状态，登录启动后按已保存状态恢复；未配置时需先打开界面完成配置。安装结束不会自动启动程序或结束其他 Deskflow 进程，请自行退出或卸载旧版本。

当前安装程序未签名，Windows 可能提示“未知发布者”，请核对下载来源及该版本的校验值。

便携版应解压到可写目录，并保留完整目录结构。运行文件是 `deskflow.exe`，仅复制 EXE 会遗漏 Qt 插件和运行库。

只需要原版 Deskflow 时，请访问[上游项目](https://github.com/deskflow/deskflow)或[上游发布页](https://github.com/deskflow/deskflow/releases)。这些上游发布包不包含本项目的文件复制扩展。

## 当前限制与缓存

| 项目 | 当前行为 |
|---|---|
| 平台 | 文件复制仅支持 Windows ↔ Windows；当前发布目标为 Windows x64 |
| 单批大小 | 最多 10 GiB |
| 单批条目 | 最多 10,000 个文件和目录条目，包含递归子项 |
| 缓存配额 | 默认 20 GiB，可设为 1–1,024 GiB；配额或磁盘可用空间不足时拒绝新的接收任务 |
| 同名选择 | 同一批次中同名的顶层文件或目录会被拒绝，不自动合并或重命名 |
| 同时复制 | 两端同时发起的冲突任务会取消，请重新复制 |
| 不支持 | 跨机剪切、拖放、断点续传、符号链接/目录联接等重解析点、跨平台文件互传、多个客户端之间经服务端中转 |

接收缓存默认位于 `%LOCALAPPDATA%\Deskflow\file-transfer`，在通常的 Windows 用户配置中位于 **C 盘**。可在“首选项 → 常规 → 管理文件缓存”选择其他本地磁盘上的专用目录，例如 `D:\DeskflowCache`，并设置容量、查看占用或清理缓存。保存首选项后重新连接，新的接收任务才使用新位置和配额。安装程序的路径选择不会自动改变缓存位置。

缓存目录必须是本地专用文件夹，不接受网络路径、磁盘根目录或符号链接/目录联接。修改位置**不会迁移或删除旧缓存**；统计占用包含该目录内的旧版缓存和其他文件，因此请勿把已有文档目录作为缓存目录。

成功接收的批次**不会自动清理**。手动清理只删除本版本创建、带管理标记的缓存批次，会保留当前 Windows 剪贴板引用的批次；接收进行中会提示稍后再试。清理前请确认已经完成目标文件夹的粘贴、无需再次使用旧缓存。旧版缓存和其他文件不会被清理工具删除；如需处理，请在确认不再使用后自行清理。当前没有自动过期清理。

不同接收批次的缓存相互隔离。最终目标文件夹已有同名文件时，由 Windows 粘贴界面决定替换、跳过或重命名。

## 验证范围

0.2.0 已通过完整 Windows Release 构建和 **33 个 CTest 测试套件**，覆盖缓存保护、清理失败、进度估算、300 个小/空文件和长路径。完整运行中依赖特殊环境的磁盘不足、符号链接和映射网络盘用例会按环境跳过；独立环境补测及双机验收结果见对应 Release 说明。

自动测试覆盖文件/目录传输、内容校验、取消与异常、路径验证、剪贴板格式、协议协商，以及既有功能回归。

2026-09-23，两台物理 Windows 主机通过 1 Gbps 直连链路进行单轮传输测量：

| 文件大小 | 方向 | 测量耗时 |
|---|---|---:|
| 100 MiB | 主控 → 副机 | 0.9008 秒 |
| 100 MiB | 副机 → 主控 | 0.9124 秒 |
| 5 GiB | 主控 → 副机 | 53.0968 秒 |

表中采用接收端同一时钟上的 `receiving → ready` 时间，不包含资源管理器从缓存粘贴到最终文件夹的额外磁盘耗时。成功批次核对了本地缓存路径、文件长度与 SHA-256。

这些数据是特定设备和网络下的单次结果，不作为吞吐保证。上述验证不表示所有文件资源管理器操作、长时间压力、磁盘不足、断线时序、杀毒软件和各 Windows 版本组合均已验收。

## 从源码构建（Windows x64）

需要支持 C++20 的 MSVC、Windows SDK、Git、CMake 3.24 或更高版本、Ninja 和 vcpkg。项目要求 Qt 6.7 或更高版本及 OpenSSL 3.0 或更高版本，具体由 [CMake 配置](CMakeLists.txt)和 [vcpkg 基线及依赖模板](cmake/vcpkg.json.in)定义。

已验证工具组合：MSVC 14.44、Windows SDK 10.0.26100、CMake 4.4.3、Ninja 1.13.2、Qt 6.11.1、OpenSSL 3.6.4。

### 1. 准备工具和源码

准备上述工具，打开已配置 **x64 MSVC 与 Windows SDK 环境**的 Developer PowerShell。将本仓库克隆到本地后进入仓库根目录。以下是可按实际磁盘修改的示例路径：

```powershell
git clone https://github.com/EugeneEvan/deskflow-filecopy.git
Set-Location deskflow-filecopy

$env:TEMP = 'D:\DevTools\tmp'
$env:TMP = $env:TEMP
$env:VCPKG_ROOT = 'D:\DevTools\vcpkg'
$env:VCPKG_DOWNLOADS = 'D:\DevTools\vcpkg-downloads'
$env:VCPKG_DEFAULT_BINARY_CACHE = 'D:\DevTools\vcpkg-cache'
New-Item -ItemType Directory -Force $env:TEMP, $env:VCPKG_DOWNLOADS, $env:VCPKG_DEFAULT_BINARY_CACHE | Out-Null

git clone https://github.com/microsoft/vcpkg.git $env:VCPKG_ROOT
& "$env:VCPKG_ROOT\bootstrap-vcpkg.bat" -disableMetrics
```

已有 vcpkg 时直接设置 `VCPKG_ROOT`，不必重复克隆。Visual Studio 安装器可能仍使用系统盘存放共享组件；上面的缓存路径设置不代表所有工具零占用 C 盘。

### 2. 配置与编译

在本仓库根目录执行：

```powershell
cmake -S . -B D:\DevTools\deskflow-filecopy-build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DVCPKG_QT=ON `
  -DBUILD_INSTALLER=OFF `
  -DBUILD_TESTS=ON `
  -DSKIP_BUILD_TESTS=ON

cmake --build D:\DevTools\deskflow-filecopy-build --parallel 4
```

此配置使用 [vcpkg manifest 模式](https://learn.microsoft.com/en-us/vcpkg/users/buildsystems/cmake-integration)恢复依赖，首次编译 Qt 可能耗时较长并占用较多磁盘。`VCPKG_QT=ON` 会由 CMake 生成仓库的 `vcpkg.json`，不要手动编辑生成结果。

`BUILD_INSTALLER=OFF` 生成程序与测试，不生成可分发安装器。`SKIP_BUILD_TESTS=ON` 仅关闭构建后的自动测试，下一步仍需单独执行验证。可执行文件位于构建目录的 `bin`，开发构建目录不一定带齐可分发运行库。

### 3. 运行测试

```powershell
ctest --test-dir D:\DevTools\deskflow-filecopy-build\src\unittests `
  --output-on-failure --interactive-debug-mode 0 --timeout 180 --no-tests=error
```

Windows CTest 配置会为测试注入 Qt DLL 和平台插件路径，避免缺少 `Qt6Gui.dll` 时反复弹窗。部分测试会访问当前 Windows 剪贴板，请先完成正在进行的复制粘贴操作。符号链接测试需要相应系统权限，否则明确报告跳过。

文件会话测试包含真实的 60 秒 ACK 超时回归，因此整套测试通常至少需要一分钟，不能把 CTest 单套超时设为 60 秒。

格式检查使用 `clang-format 20.1.0`，与仓库 CI 约定保持一致。上游一般构建说明可参考 [Deskflow Building](https://github.com/deskflow/deskflow/wiki/Building)，本扩展的配置以上述内容和当前源码为准。

### 4. 制作安装程序和便携包

使用 [Windows 打包脚本](deploy/windows/package-filecopy.ps1)从已验证的 Release 构建制作 ZIP、安装程序和 SHA-256 清单。需要 PowerShell 5.1 或更高版本、Inno Setup 6.7 或更高版本、MSVC 的 x64 `dumpbin.exe`，以及与构建匹配的 vcpkg 运行库、Qt 插件和 MSVC 可再分发 CRT。脚本不会安装这些依赖，也不会编译源码或签名程序。

以下示例需将工具版本目录替换为本机实际路径；`DependenciesDirectory` 应指向当前构建实际使用的 `x64-windows` 依赖目录，manifest 模式通常位于构建目录的 `vcpkg_installed` 下：

```powershell
.\deploy\windows\package-filecopy.ps1 `
  -BuildDirectory D:\DevTools\deskflow-filecopy-build `
  -DependenciesDirectory D:\DevTools\deskflow-filecopy-build\vcpkg_installed\x64-windows `
  -CrtDirectory D:\DevTools\MSVC\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT `
  -Dumpbin D:\DevTools\MSVC\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe `
  -Iscc D:\DevTools\InnoSetup\ISCC.exe `
  -OutputDirectory D:\Releases\deskflow-filecopy-0.2.0 `
  -Version 0.2.0 `
  -ProjectUrl https://github.com/EugeneEvan/deskflow-filecopy
```

输出目录必须位于源码仓库之外，已有同名发布文件时脚本会拒绝覆盖。打包会收集运行库、插件、许可与中性初始配置，不复制开发机的现有用户配置。发布时还需提供对应标签的源码，并在干净环境验证生成的安装程序和便携包。

## 维护与贡献

本仓库由 [@EugeneEvan](https://github.com/EugeneEvan) 维护，只有仓库主人直接写入和发布，不向普通使用者授予协作者写权限，不自动合并外部提交。公众可以拉取源码、[提交问题](https://github.com/EugeneEvan/deskflow-filecopy/issues)、fork 后修改并发起 Pull Request，由仓库主人决定是否采纳。

这一维护政策只约束本仓库的写入流程，不限制许可证授予的使用、复制、修改和再分发权利。详见[贡献与维护政策](.github/CONTRIBUTING.md)。

本扩展的问题请在本仓库提交；不要将未确认属于上游的问题直接派给 Deskflow 上游维护者。

## 上游归属与许可

感谢 [Deskflow 及其贡献者](https://github.com/deskflow/deskflow/graphs/contributors)。本项目保留上游源文件的版权声明、[LICENSE](LICENSE)、[LICENSES](LICENSES) 和 [REUSE.toml](REUSE.toml)，不将上游工作声明为本项目独立原创。

主要应用代码沿用 **GPL-2.0-only** 及文件声明中的 [OpenSSL 例外](LICENSES/LicenseRef-OpenSSL-Exception.txt)。构建脚本、文档、图标和第三方组件可能采用不同许可证，以各文件 SPDX 声明、`REUSE.toml` 和附带许可为准。发布二进制时应一并提供对应版本源码及必要的许可文件。

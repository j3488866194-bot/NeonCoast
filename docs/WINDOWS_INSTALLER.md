# Windows 安装版与便携版

发行目标为 Windows 10 2004 或更新的 x64 系统。玩家无需 Unreal Editor。
安装器以当前用户权限安装，默认目录为 `%LOCALAPPDATA%\Programs\NeonCoast`，
支持自选位置、桌面及开始菜单快捷方式、Windows 设置中的卸载。
卸载仅移除安装器写入的文件，保留游戏生成的 `Saved` 文件夹和用户存档。
发行版的具体测试结果以 `UE_VERIFICATION.md` 为准。

## 制作发行文件

先完成 UE cook/package 及该包的运行验证，再执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package-windows.ps1 -Version 0.4.1
```

输出位于 `builds/releases/0.4.1/`：

- `NeonCoast-0.4.1-Windows-Setup.exe`：完整安装器。
- `NeonCoast-0.4.1-Windows-Portable.zip`：完整解压后直接运行。
- `SHA256SUMS.txt`：两个发行文件的 SHA-256。
- `build-manifest.json`：主程序和 cooked 资源的 SHA-256，便于与测试报告核对。

Manifest 同时记录 `Unreal/Source` 和 `Unreal/Config`（排除私有密钥配置）逐文件
哈希清单的合并 SHA-256。如果在提交前构建，`source_commit` 是当时 HEAD 基线，
`source_worktree_dirty` 明确标记未提交改动；不得把基线冒称完整发行源码提交。
外层 GitHub Release 说明记录最终源码提交，EXE/内容哈希用于绑定实际通过测试的包。

脚本每次创建新的 `artifacts/installer-staging/<run-id>/`，不从上次 staging
增量复制。排除 `Saved`、崩溃、日志、QA、PDB/调试符号、UE staging 清单与独立
Crypto/Encryption 密钥配置。原始游戏目录不被修改。保留所有运行时 DLL 与资源，
同步最新发行许可、项目许可、素材来源通知、UE 第三方组件许可及原包 `NOTICES.txt`。
`SHA256SUMS.txt` 也包含在解压目录，可逐文件检查发行内容。
源包和洁净 staging 均必须通过 `tools/verify-ue-encryption.py`。此处直接读取
真实容器加密标记并排查裸密钥文件，失败就停止发行打包；不把配置开关当成成功证据。

常用参数：

```powershell
# 无需安装器编译器：只制作便携 ZIP
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package-windows.ps1 -Mode PortableOnly

# 只检查并生成洁净目录，不创建最终发行文件
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package-windows.ps1 -Mode StageOnly

# 指定本机位置；最终包准备好后重建同版本可加 -Force
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package-windows.ps1 `
  -Engine 'D:\Work_software\UnrealEngine\UE_5.8' `
  -Compiler 'D:\NeonCoastUE\.tools\inno-6.7.3\ISCC.exe' -Force
```

`-Force` 只替换输出目录下准确命名的该版本 EXE/ZIP，不递归删除任何目录。
`artifacts/windows-distribution-build.json` 记录本次 staging、排除文件和发行哈希。
不要在 UE 正在重建源包时运行最终打包，否则它会读取尚未完成的文件。

## 安装器编译器

使用 **Inno Setup 6.7.3**，从 [官方下载页](https://jrsoftware.org/isdl.php)
或 [官方 GitHub 发布](https://github.com/jrsoftware/issrc/releases/tag/is-6_7_3) 获取。
不需要付费扩展。其 [官方许可](https://jrsoftware.org/files/is/license.txt)
允许包括商业应用在内的使用；必须保留 Inno 自身版权和来源。

官方安装文件：`innosetup-6.7.3.exe`。
官方 GitHub asset digest：

```text
9c73c3bae7ed48d44112a0f48e66742c00090bdb5bef71d9d3c056c66e97b732
```

下载后先核对 SHA-256 和 Authenticode 签名（发布者 `Pyrsys B.V.`）。
可在安装向导选择当前用户并安装到项目 `.tools/inno-6.7.3/`，无需修改系统 PATH。
打包脚本不会自动下载或执行第三方安装程序；若编译器不存在，明确报错，
`PortableOnly` 和 `StageOnly` 仍可使用。

安装器源码为 `tools/installer/NeonCoast.iss`，包含简体中文和英文正常安装流程。
图标由
`tools/installer/New-Icon.ps1` 按项目现有 `assets/icon.svg` 的形状与颜色生成，
没有改动游戏中的 Opus 界面。Inno Setup 制作的安装器不是代码签名证书；
本项目未购买签名证书，发行文件的发布者签名状态须如实记录。

## 运行依赖

从本机 UE 的 `Engine/Extras/Redist/en-us/vc_redist.x64.exe` 复制微软原版
Visual C++ x64 运行库，记录文件版本和 SHA-256。安装器检查原生 x64 注册表
中的运行库版本，仅在缺失或过旧时启动该组件的静默安装。微软系统组件可能
触发管理员确认；取消或安装失败会停止安装并给出错误，不把失败当成成功。
已经有兼容运行库的机器不会触发这个步骤。便携版也保留该运行库供手动安装。

UE 同目录中的 GameInput MSI 不在当前包的必需依赖中：当前项目没有启用
GameInput 插件，常规 Xbox/XInput 手柄路径由 Windows 与 UE 现有运行库提供。
不要为当前包额外修改系统的 GameInput 服务。第三方驱动/非标准手柄的物理
兼容性仍须用对应硬件验证。

## 安装与卸载验证

可复现验收命令：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/installer/Verify-Installer.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/installer/Verify-Portable.ps1
```

第一条在 `artifacts/installer-verification/<run-id>/` 安装真实发行 EXE，检查
所有已安装文件的 SHA-256、快捷方式、当前用户卸载注册、版本和卸载存档保留。
发现已有同名安装/快捷方式时会停止，避免覆盖玩家环境。第二条从 ZIP 内逐文件
解压读取并核对 SHA-256，检查包内没有 Saved、调试符号或独立密钥配置。
它们不代替游戏运行、画面或物理手柄测试。

测试必须使用最终发行包及独立的临时安装目录，避免覆盖玩家现有存档。
示例静默参数：

```powershell
# 使用绝对路径替换下面两个位置；此命令不会启动游戏
Start-Process -FilePath '...\NeonCoast-0.4.1-Windows-Setup.exe' `
  -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /DIR="...\installer-test" /TASKS=""' `
  -WindowStyle Hidden -Wait -PassThru
```

检查 EXE/资源与 build manifest 一致，真实启动到主菜单，确认正常退出。
在该测试目录的 `NeonCoast/Saved` 内创建测试哨兵，然后运行它自己的
`unins000.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART`，确认程序文件移除、
哨兵保留、仅测试安装的卸载注册表项移除。安装器的默认启动复选框应在
静默模式下跳过。测试报告须记录安装/卸载退出码、程序与安装器哈希及验证范围。

## 已安装版本的原位升级验证

`Verify-Installer.ps1` 的隔离安装/卸载测试不适用于已经安装正式版的电脑。
保留正式安装并验证升级时，先关闭游戏，再运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/installer/Verify-Upgrade.ps1 `
  -Installer 'D:\NeonCoastUE\builds\releases\0.4.1\NeonCoast-0.4.1-Windows-Setup.exe' `
  -ExpectedManifest 'D:\NeonCoastUE\builds\releases\0.4.1\build-manifest.json'
```

该脚本会真正更新现有安装，不是只读预演。它只接受 HKCU 已注册且路径恰好是
当前用户 `%LOCALAPPDATA%\Programs\NeonCoast` 的 Neon Coast，拒绝改装到其他
位置和降级。安装前将以下数据完整备份至独立的
`artifacts/upgrade-verification/<run-id>/backup/`，逐文件核对 SHA-256：

- 安装目录内所有 `Saved` 文件夹，包括隐藏文件和空目录。
- `%LOCALAPPDATA%\NeonCoast\Saved` 中的全部现有存档和设置。
- 旧发行 SHA256 清单之外的用户文件，以及与旧清单哈希不同、已被用户修改的发行文件。

例如 `游戏操作说明.txt` 如果仍与旧发行清单一致，就属于未修改的受管理文档，
可以随新版更新；如果被修改过，或本来不在清单中，则备份并保护其现有内容。

备份不跟随 junction/symlink；遇到路径重定向或备份期间文件变化就停止。
脚本保存本机 `backup-manifest.json`，再使用已验证的注册目录静默运行安装器，
带 `/NORESTART`，在等待之前保留进程原生句柄，以准确读取退出码。
它检查所有新版 payload 校验和、主程序/manifest、注册版本与目录、桌面及
开始菜单快捷方式，并再次核对每一个受保护文件。

如果安装器改变或删除受保护数据，脚本尝试从已验证的备份恢复，且仍判失败。
如果新版 payload 与需要保留的旧用户文档内容冲突，不能同时宣称两者校验通过；
应调整发行清单后重建，不得悄悄覆盖用户文档。无论成功或失败，脚本都不会
执行卸载、回滚程序版本、启动游戏或从其他试玩目录迁移存档。备份持续保留，
结果写入 `artifacts/windows-upgrade-verification.json` 及本次独立目录的 `report.json`。

v0.4.1 的命令示例和验收工具不会把 v0.4.0 的历史安装/卸载结果当作新版通过证据。
最新试玩目录的存档选择及迁移是升级之后的独立步骤，需要单独备份与验证。

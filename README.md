# NEON COAST · 霓虹海岸

## UE 5.8 迁移工程（开发中）

当前新工程位于 `D:\NeonCoastUE\Unreal\NeonCoast.uproject`，Git 分支为 `ue5-migration`。
原 Godot 工程保留在 `D:\百度招新作业`；下方旧版截图、功能列表与 213 项测试记录只适用于 Godot 版。

已编写 UE 城市、驾驶与跳车、行人和交通、HUD，以及命令行构建/资源生成/检查入口。
城市外观、HUD 和材质脚本由 Claude Opus 5.5 high 生成。引擎仍在官方启动器中安装；
**UE C++ 尚未编译，UE 游戏尚未运行，也没有可交付的 UE 可执行文件。**

安装完成后从此目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File tools/unreal.ps1 -Action Test
powershell -ExecutionPolicy Bypass -File tools/unreal.ps1 -Action Editor
```

第一条会编译、生成资产并执行运行时检查；第二条打开编辑器并启动本机原生 MCP。
完整入口、当前实现边界与后续验证项目见 [UE 工作流](docs/UE_MIGRATION.md)。

## 旧 Godot 版本记录

**海风、霓虹，以及一份不该接下的快递。**

本机工程根目录：`D:\百度招新作业`。游戏源码、Git 历史、测试和构建产物均在这个目录内。

用 Godot 4.7.2 制作的原创 3D 开放世界动作原型。故事围绕海滨城市中的四次行动展开：海风来信、潮汐账本、落日跑线和最后一程，包含取件送货、摆脱追捕和限时驾车。完成故事后可继续自由漫游。

本工程不是 Rockstar 的 GTA VI，不包含官方游戏代码、角色、音乐、标识或泄露素材。当前画面是程序生成的风格化场景，并非 AAA 写实制作。

![实际游戏标题界面](docs/images/title.png)
![实际驾驶画面](docs/images/gameplay.png)

## 直接游玩

- Windows 64 位：解压发布包，双击 `NeonCoast.exe`，无需安装 Godot。
- 源码工程：双击 `启动游戏.cmd`。优先运行已导出的版本，没有导出时使用本机 Godot。
- 编辑工程：双击 `打开工程.cmd`，或用 Godot 4.7.2 打开 `project.godot`。
- 全程支持离线单人游玩。需要支持 OpenGL 3.3 的显卡。默认窗口可调整尺寸；设置中可切换全屏、画质、音量、视角灵敏度和 Y 轴反转。

初次进入后靠近橙色跑车，按 **F / 手柄 Y** 上车。键盘 **WASD** 驾驶，手柄 **RT / LT** 加速、刹车及倒车，左摇杆转向。**M / View** 打开地图，**Esc / Menu** 暂停。所有操作见 [操作说明](docs/CONTROLS.md)。

## 已实现内容

- 可驾驶城市道路、不同街区、海滨栈道和可步行沙滩；昼夜、海面、棕榈、建筑灯光与交通标线。
- 第三人称步行、奔跑、跳跃、瞄准、射击，上下车、车辆碰撞与损坏、手刹、加速、灯光、喇叭和道路救援。
- 12 辆循环行驶的民用车辆、停放车辆、32 名行人及受惊反应，按通缉等级生成的巡逻车和视线追踪。
- 四个顺序任务、一次限时路线挑战、10 张可收集明信片、现金奖励、车辆修理和医疗重生。
- 小地图、道路导航、自定义地图标记、任务提示、暂停和设置菜单，手柄/键鼠提示自动切换。
- 两套原创合成器电台、引擎声、海风、警笛及交互音效；照片模式和本地截图。
- 自动存档、读取、有效旧存档备份与损坏文件保护；手柄断开或窗口失焦时自动暂停。

这是可扩展的垂直切片：无联机、室内场景、完整城市交通规则、复杂人物剧情或 AAA 级角色资产。任务和追捕是轻量系统。手柄绑定通过合成事件验证；物理手柄的具体型号、蓝牙连接和振动手感仍需实机确认。

## 开发与验证

```powershell
# 安装经过官方 SHA-512 核验的引擎
powershell -ExecutionPolicy Bypass -File tools/setup-engine.ps1
# 如果也需要在新电脑导出 Windows，连同官方导出模板一起安装
powershell -ExecutionPolicy Bypass -File tools/setup-engine.ps1 -WithExportTemplates
# 自动化检查，不写入真实游戏进度
powershell -ExecutionPolicy Bypass -File tools/verify.ps1
# 导出 Windows：需要 Godot 4.7.2 的 Windows 导出模板
powershell -ExecutionPolicy Bypass -File tools/build.ps1
```

引擎默认位于 `D:\Work_software\Godot\4.7.2`；运行脚本也支持 `GODOT_EXE` 环境变量。Windows 导出包写入 `builds/`，源码、字体、测试和文档由 Git 管理；引擎、缓存、存档、构建产物、日志和凭据不进入仓库。

运行数据保存在 Godot 的 `user://` 目录（Windows 通常为 `%APPDATA%\Godot\app_userdata\Neon Coast · 霓虹海岸`）。照片位于其 `screenshots` 子目录。自动化测试使用隔离文件；`--test-mode` 禁止写入正式进度及设置。

六组自动化检查共 213 项通过，并完成 14 张原生渲染截图，覆盖 720p、900p 和 1440p；实际验证记录见 [VERIFICATION.md](docs/VERIFICATION.md)。Claude Code 已连接用户指定的 Claude Opus 5.5；地图界面由其 high 档生成。完整 HUD 请求多次超限或出现重复输出，最终主界面、游戏系统与整合由 Codex 及协作任务完成，具体边界见 [工具链记录](docs/TOOLCHAIN.md)。

## 同步 GitHub

已建立本地 `main` 分支。创建空 GitHub 仓库后，在工程目录执行：

```powershell
git remote add origin https://github.com/YOUR_ACCOUNT/NeonCoast.git
git push -u origin main
git push origin --tags
```

`builds/` 中的 Windows ZIP 适合上传到 GitHub Release，避免把引擎或可执行文件提交到源码历史。项目尚未自动创建远程仓库或公开发布。

## 许可与素材

项目源码与原创内容采用 MIT 许可。Godot 为 MIT；Barlow Condensed 和 Noto Sans SC 字体采用 SIL OFL 1.1。具体来源和完整字体许可见 [第三方说明](THIRD_PARTY_NOTICES.md) 与 `assets/fonts/`。所有场景几何与声音均由工程源代码生成。

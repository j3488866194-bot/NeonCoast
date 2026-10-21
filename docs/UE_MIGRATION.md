# Unreal Engine 迁移记录

更新：2026-10-21。当前阶段为源码准备完成，等待引擎安装。以下实现均需 UE 编译和运行验证。

## 位置与工具链

- 新工程：`D:\NeonCoastUE\Unreal\NeonCoast.uproject`，分支 `ue5-migration`。
- 原工程：`D:\百度招新作业`，保留原 Godot 版本和历史；本目录从本地仓库克隆。
- 引擎目标：官方 Epic Games Launcher 的 UE 5.8.4，安装路径 `D:\Work_software\UnrealEngine\UE_5.8`。
- 已安装编译器：Visual Studio 2022 Build Tools 17.14.42，含 C++ 工具集，位于 `D:\Work_software\VSBuildTools2022`。
- 原生 MCP：启用 `ModelContextProtocol` 与 `AllToolsets`，仅 Editor 目标；编辑器通过启动参数监听 `http://127.0.0.1:8008/mcp`。
- MCP 命令行客户端使用独立 `.venv-mcp` 和官方 Python MCP SDK 1.30.0，不修改全局 Python 包。

## 命令行操作

从 `D:\NeonCoastUE` 执行：

```powershell
# 只检查引擎和 C++ 工具链，不执行编译
powershell -ExecutionPolicy Bypass -File tools/unreal.ps1 -Action Check

# 编译 Editor 模块
powershell -ExecutionPolicy Bypass -File tools/unreal.ps1 -Action Build

# 编译后用 UnrealEditor-Cmd 创建材质和入口地图
powershell -ExecutionPolicy Bypass -File tools/unreal.ps1 -Action Prepare

# 编译、生成资产、在无渲染游戏进程中执行行为检查
powershell -ExecutionPolicy Bypass -File tools/unreal.ps1 -Action Test

# 打开编辑器，同时启动原生 MCP；也可双击 打开UE5工程.cmd
powershell -ExecutionPolicy Bypass -File tools/unreal.ps1 -Action Editor

# 通过编辑器的 -game 模式游玩；也可双击 启动UE5.cmd
powershell -ExecutionPolicy Bypass -File tools/unreal.ps1 -Action Play

# 通过 RunUAT 编译、Cook、打包 Windows Development 版本
powershell -ExecutionPolicy Bypass -File tools/unreal.ps1 -Action Package
```

所有行动使用同一入口。`Build` 之后的各行动会先重新执行增量编译，再检查资源生成完成标记。
安装在其他位置可传 `-Engine '绝对引擎目录'`；更改 MCP 端口可传 `-McpPort 8010`，并同步客户端 URL。
日志位于 `artifacts/ue-*.log`，安装检测写入 `artifacts/ue-toolchain.json`。
打包目标为 `builds/NeonCoast-UE-Windows`。当前这些构建日志、地图资产与可执行文件尚未生成。

## MCP 操作

先启动编辑器，等启动日志确认 MCP 服务绑定端口，再执行：

```powershell
.\.venv-mcp\Scripts\python.exe tools/ue_mcp.py --output artifacts/ue-mcp-tools.json list
.\.venv-mcp\Scripts\python.exe tools/ue_mcp.py call list_toolsets
# 根据实际返回的工具 schema，把参数写入 JSON 文件再调用
.\.venv-mcp\Scripts\python.exe tools/ue_mcp.py call describe_toolset --args-file artifacts/toolset-query.json
```

客户端按顺序调用工具。先读取实际 schema，再操作场景、材质或测试，不预设工具参数。
根目录 `.mcp.json` 可供 Claude Code 连接同一编辑器。原生 MCP 是 UE 的实验性功能；本工程只在编辑器启动时启用。
如果需要重建客户端环境：

```powershell
python -m venv .venv-mcp
.\.venv-mcp\Scripts\python.exe -m pip install -r tools/requirements-mcp.txt
```

## 已编写的内容与边界

- 500 米城市基础地面、海滨道路、装饰艺术酒店、海滩、棕榈、晨光、HISM 批量场景和 Canvas HUD。
- 第三人称步行、奔跑、跳跃与上车；行驶时可跳车，检查出口空间、传递动量、施加伤害与短暂恢复。
- 驾驶采用有碰撞扫掠的运动学原型、限制积分步长、平滑转向和摄像机子步跟随。当前尚未使用 Chaos 悬挂/轮胎模型，不能称为 RAGE 或完整车辆物理复刻。
- 计划生成 64 名行人和 8 辆行驶车辆，加 1 辆初始可驾驶车辆。行人有往返步行、靠近车辆受惊和问候行为，尚需验证实际生成与移动。
- 角色和车辆使用程序几何占位外观；未引入写实模型、骨骼动画或完整 City Sample 资产。
- 旧版任务、枪械、追捕、收集、存档、音频与设置菜单尚未迁移。旧版测试不能证明 UE 版完成。

UE 版操作：WASD 行走/驾驶，鼠标视角，F 上车/下车/高速跳车；步行空格跳跃、Shift 奔跑；驾驶空格刹车、Shift 加速；E 问候/维修点修车；T 救援复位；Esc 暂停。
手柄：左杆移动/转向、右杆视角、Y 上下车、A 跳跃/车辆加速、B 刹车、RT/LT 油门/倒车、左杆按下奔跑、X 交互、方向下救援、Start 暂停。物理手柄未验证。

## 当前验证结果

已通过 Python 文件语法编译、PowerShell AST 语法解析、MCP 客户端 SDK 导入及帮助入口。
工具链检查确认 C++ 工具已安装，但引擎编辑器尚不可用。MCP 配置已写好，尚未连接真实 UE 服务。

后续必须完成：

1. UE C++ 编译并修正 API/依赖问题，执行资产生成，验证打包依赖。
2. `-Action Test` 检查运行时车辆/行人数、NPC 移动、30/60/144 FPS 行驶距离、平路高度稳定、上车、高速跳车动量/伤害/落地、恢复后跳跃。
3. 有渲染窗口实机检查视觉、镜头抖动、键鼠输入、碰撞出口、帧率和 HUD。无渲染检查不能替代这些验收。
4. 连接 MCP，读取工具列表并完成实际场景调用。
5. 继续美术与原版玩法迁移，制作并启动 Windows 打包版本。

## Opus 视觉贡献

Claude Code 以 `claude-opus-5-5 --effort high` 完成城市 `NCCityVisual.h/.cpp`、HUD `NCHUD.h/.cpp` 和材质生成 `build_visual_assets.py` 五个文件。
主任务负责引擎迁移、游戏控制和整合，并为 HUD 补充了 `Engine/World.h` 显式头文件依赖。
模型回执摘要与整合后文件哈希见 `UE_OPUS_RECEIPT.json`；完整本地运行回执在忽略提交的 `artifacts/opus-ue5-visual.jsonl`。

参考：[Epic 官方 Unreal MCP 文档](https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor?application_version=5.8)、[官方 MCP Python SDK](https://github.com/modelcontextprotocol/python-sdk)。

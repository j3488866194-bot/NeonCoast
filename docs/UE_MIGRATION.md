# Unreal Engine 工作流与操作

更新：2026-10-21。UE 5.8.4 Windows 独立包完成打包，23:16:08 的最终检查为 117 项全部通过（95 项玩法、22 项警方集成），另有 2 项存档自动化测试通过。独立 DX12 游戏正常关闭，退出码 0、无引擎错误；同包 1600 × 900 原生截图和 720 帧渲染检查通过。Kenney 人物、车轮、Opus 世界标记/金币与黄昏光照已目视确认，MCP 的 E/F/M/F12 操作也已实测。音频已初始化，混音未试听。证据见 [UE_VERIFICATION.md](UE_VERIFICATION.md)。

## 位置与工具链

- 工程：`D:\NeonCoastUE\Unreal\NeonCoast.uproject`，分支 `ue5-migration`。
- 原 Godot 工程：`D:\百度招新作业`，保留原版本和历史。旧版测试、截图与四段故事不作为 UE 版的完成证据。
- 引擎：Epic 官方 UE 5.8.4，`D:\Work_software\UnrealEngine\UE_5.8`。
- 编译器：VS 2022 Build Tools 17.14.42，`D:\Work_software\VSBuildTools2022`。当前构建使用 MSVC 14.44 与 Windows SDK 10.0.26100.0。
- 原生 MCP：`ModelContextProtocol`、`AllToolsets` 仅在 Editor 目标启用，端口默认为 `http://127.0.0.1:8008/mcp`。客户端使用 `.venv-mcp` 中的官方 Python MCP SDK。

## 构建、运行与 MCP

从 `D:\NeonCoastUE` 执行：

```powershell
# 初次准备时生成原创 WAV；本机已经生成。需要 host Python 和 NumPy。
python tools/generate-audio.py

# 检查 / 编译 / 编辑器资产生成
powershell -ExecutionPolicy Bypass -File tools/unreal.ps1 -Action Check
powershell -ExecutionPolicy Bypass -File tools/unreal.ps1 -Action Build
powershell -ExecutionPolicy Bypass -File tools/unreal.ps1 -Action Prepare

# UE 的 -game 模式；也可双击 启动UE5.cmd
powershell -ExecutionPolicy Bypass -File tools/unreal.ps1 -Action Play

# 隔离玩家存档的玩法、警方集成和两项存档自动化测试
powershell -ExecutionPolicy Bypass -File tools/unreal.ps1 -Action Test

# 编辑器与原生 MCP；也可双击 打开UE5工程.cmd
powershell -ExecutionPolicy Bypass -File tools/unreal.ps1 -Action Editor

# RunUAT 编译、Cook、打包 Windows Development 版
powershell -ExecutionPolicy Bypass -File tools/unreal.ps1 -Action Package

# 仅在独立 Windows 包生成后：验证打包程序的行为并记录 EXE 哈希
powershell -ExecutionPolicy Bypass -File tools/verify-ue-release.ps1

# 独立 DX12 渲染、原生截图和正常退出；使用本轮独有文件标识
powershell -ExecutionPolicy Bypass -File tools/verify-ue-render.ps1
```

默认[启动游戏.cmd](../启动游戏.cmd)优先运行[已打包 Windows 程序](../builds/NeonCoast-UE-Windows/Windows/NeonCoast.exe)，无需打开编辑器；`打开工程.cmd` 指向 UE，`启动旧版Godot.cmd` 保留旧版入口。`Prepare` 及其后的行动会先增量编译，再生成材质、人物、音频与入口地图。引擎安装在其他位置时传 `-Engine '绝对引擎目录'`。日志位于 `artifacts/ue-*.log`，工具链检测为 `artifacts/ue-toolchain.json`，打包目录为 `builds/NeonCoast-UE-Windows`。复制游戏时需完整保留其中的 `Windows` 文件夹；开发用 `Play` 仍依赖本机 UE。当前最终包已通过独立运行的 117 项检查。

`Package` 为每次运行创建 D: 工程下专用 UAT 日志目录，并在当前进程的 `NO_PROXY` 中补入 `[::1]`，避免 EFS 日志复制与 Zen 回环代理问题。仅编辑器所需的 `GameFeatureData` 扫描已设为 editor-only，普通包不再误扫不存在的运行时类。最终 `artifacts/ue-package.log` 记录 Cook 36.69 秒、0 errors / 0 warnings，BuildCookRun 83.82 秒、`BUILD SUCCESSFUL`、退出码 0。人物重复导入安全复用三个材质，见 `artifacts/ue-prepare.log`。

开发游戏与烟测启动使用 `-DisablePython`，避免运行游戏时初始化仅编辑器需要的 Python 工具类型。资产生成阶段仍启用 Python。`Test` 的自动化报告位于 `artifacts/ue-automation-时间戳/index.json`；最初已通过的报告另存于 `artifacts/ue-automation/index.json`。发行报告 `artifacts/ue-release-verification.json` 记录真正运行的 EXE 及 Pak/IoStore 内容文件大小、SHA-256 和检查数；`artifacts/ue-release-render-verification.json` 记录独立渲染的运行标识、截图/CSV 哈希及退出状态。

编辑器启动后，使用实际工具 schema 操作：

```powershell
.\.venv-mcp\Scripts\python.exe tools/ue_mcp.py --output artifacts/ue-mcp-tools.json list
.\.venv-mcp\Scripts\python.exe tools/ue_mcp.py call list_toolsets
.\.venv-mcp\Scripts\python.exe tools/ue_mcp.py call describe_toolset --args-file artifacts/toolset-query.json
```

先读取工具列表，再调用场景、Python 或测试工具；调用顺序执行。端口可通过 `-McpPort 8010` 更改，并同步客户端 URL。根目录 `.mcp.json` 可供 Claude Code 连接。原生 MCP 为实验性功能，本机已实际调用 `EditorToolset.EditorAppToolset.StartPIE`、`SlateInspectorToolset.SlateInspectorToolset.PressKey` 和 `EditorToolset.EditorAppToolset.CaptureEditorImage`。新版实际操作包括 E 接任务、F 上车、M 地图与 F12 截图；日志为 `artifacts/ue-characters-editor.log`、`artifacts/ue-final-editor.log`，当前图片为 `docs/images/ue/first-view.png`、`driving.png`、`map.png`。

## UE 版操作

| 行为 | 键鼠 | Xbox 式手柄 |
| --- | --- | --- |
| 步行 / 转向 | WASD | 左摇杆 |
| 视角 | 鼠标 | 右摇杆 |
| 上车、下车、高速跳车 | F | Y |
| 步行跳跃 | Space | A |
| 步行奔跑 | Left Shift | 按下左摇杆 |
| 步行交互 / 问候 / 任务 | E | 方向键右 |
| 瞄准 / 射击 | 鼠标右键 / 左键 | LT / RT |
| 换弹 | R | X |
| 驾驶油门 / 倒车 | W / S | RT / LT |
| 驾驶刹车 / 加速 | Space / Left Shift | B / A |
| 车内任务 / 维修 / 医疗交互 | E | X |
| 车灯 | L | 方向键上 |
| 电台切换 / 关闭 | Q | 方向键左 |
| 车辆喇叭 | H | 按下左摇杆 |
| 救援复位 | T | 方向键下 |
| 地图 | M | View |
| 暂停 / 返回 | Esc | Menu / Start |
| 操作帮助 | F1 | — |
| 暂停时画质低 / 中 / 高 | 1 / 2 / 3 | — |
| 窗口 / 无边框全屏 | F11 | — |
| 隐藏 HUD 的照片视图 | F6 | — |
| 保存截图 | F12 | — |

打开地图后可鼠标点击设置标记，Delete 清除标记。地图和帮助会暂停世界。截图写入当前 UE `ProjectSavedDir()/Screenshots`。照片视图仅隐藏 HUD，不是自由飞行摄影机。物理手柄、不同设备布局和实际振动未完成验收。

## 已实现的可玩循环

**驾驶与城市。** 使用 Chaos 四轮车辆、Epic 官方 SportsCar 骨骼/物理/动画资产、转向与刹车、车损、灯光、喇叭和安全出口检测。高速跳车继承速度并产生伤害与短暂恢复。主街区约 500 米宽，包含海滨道路、海滩、酒店、棕榈、招牌和远景；场景按批次生成，支持昼夜变化。初始生成 64 名行人、8 辆行驶交通车和 1 辆玩家附近车辆；警方响应另外生成车辆。

**行动与追捕。** 步行、奔跑、跳跃、肩部瞄准、带枪口遮挡检测的射击、12 发弹匣与有限备弹、换弹、伤害和死亡恢复。行人可问候、受惊逃离和受伤。犯罪提高 0–3 级通缉；警车根据目击和最后报告位置追逐/搜索，持续目击或新犯罪阻止热度衰减。22 项警方集成检查实测正常导演派车、真实物理移动、视线更新、遮挡后搜索、废车替补和真实时间消退；完整城市追逐的手感仍需连续游玩调校。

**NIGHT COURIER。** 出生点附近海岸仓库接受任务，开车到内陆交付点并停下交互，摆脱搜索后返回领取 $750。任务可重复。当前 UE 版是一项三阶段任务，没有宣称迁移完旧 Godot 的四段故事。

**COAST CIRCUIT。** 南侧道路出发，95 秒内按顺序驾驶通过 8 个道路检查点，奖励 $450、记录最佳时间。步行或经过后续检查点不计数。任务期间不能开始比赛；重启后须到起点重新发起比赛。自动检查传送定位各门验证规则，没有证明玩家已驾驶跑完全程或难度已经调校。

**探索和服务。** 8 枚海岸代币需要步行靠近，每枚 $75，全部找到另加一次 $400。车库维修 $100，诊所医疗 $75；没有损伤时不扣款。死亡最多收取 $125，取消当前任务/比赛并回到安全点。

**导航与声音。** 地图、小地图、目标提示、距离、现金、通缉和交互信息来自当前游戏状态；世界标记与金币使用可见网格，支持自定义地图标记、暂停、画质选择和截图。两个原创 16 秒合成器电台只在车内播放，另有速度驱动引擎声、海滨环境、附近警车警笛、喇叭、枪声、换弹和脚步。实机日志确认两个电台与四个循环声实例初始化；实际混音与试听仍待验收。

## 存档与边界

`NeonCoastProgress_v1` 保存现金、代币、完成任务数、比赛胜数/纪录、任务阶段、通缉热度与最后犯罪位置；上一个有效存档保存在 `NeonCoastProgress_v1_backup`。字段不合法时尝试读取备份。开发工程通常位于 `Unreal/Saved/SaveGames`，打包版由 UE 平台存档目录规则决定。不会恢复正在进行的比赛、玩家位置或电台状态。

`-NeonCoastSmokeTest` 禁止读取和写入正式进度，并禁用音频。存档自动测试使用内存序列化；真实磁盘上的损坏主档/备用档恢复、跨进程重开和用户机器权限仍需单独验收。

Kenney CC0 人物骨骼、三种人类服装和 Idle/Run/Jump 动画已实际运行；当前画面确认主角面向、待机和脚底对齐正常，日志确认主角加 64 名行人共 65 个骨骼人物。源模型实测高 376.468611 cm，缩放约 0.472815 对应 178 cm；没有单独步行、瞄准、换弹或死亡动画，行走暂由较慢 Run 表现。行人和交通规则较简单。没有联机、完整室内系统、船舶/飞机、完整伙伴剧情、GTA VI 原作地图/角色/音乐、完整 City Sample 或 AAA 动画资产。通缉、任务、比赛和城市规模均为本工程原创原型实现，不代表 GTA VI 的具体机制。

## 参考、视觉贡献与许可

- 本次资讯依据与未证实内容边界：[GTA_REFERENCE_NOTES.md](GTA_REFERENCE_NOTES.md)。
- Opus 5.5 high 第一轮城市、HUD、材质：[UE_OPUS_RECEIPT.json](UE_OPUS_RECEIPT.json)。
- 第二轮改进城市、昼夜、招牌、地图与 HUD；五个文件写入后会话达到限额：[UE_OPUS_SECOND_PASS_RECEIPT.json](UE_OPUS_SECOND_PASS_RECEIPT.json)。不能把限额退出描述为模型完整验收成功。Codex 负责 API 适配、Chaos 与玩法整合、编译和行为验证。
- 第三轮 Opus 5.5 high 实际查看人物素材和游戏截图，选定服装、身高及材质：[视觉方案](UE_CHARACTER_ART_DIRECTION.json)、[成功回执](UE_CHARACTER_OPUS_RECEIPT.json)。人物模型和动画由 Kenney 创作，素材采用 CC0；来源、导入结果及警告见 [KENNEY_CHARACTERS.md](licenses/KENNEY_CHARACTERS.md)。
- 后续 Opus 5.5 high 完成世界标记与金币，并查看实拍画面调整黄昏阴影可读性：[标记回执](UE_MARKERS_OPUS_RECEIPT.json)、[光照回执](UE_DUSK_OPUS_RECEIPT.json)。
- Epic 车辆资产和参考代码保留第三方许可：[EPIC_VEHICLES_PROVENANCE.md](licenses/EPIC_VEHICLES_PROVENANCE.md)。
- 原创音频与精确资产路径：[AUDIO_PROVENANCE.md](AUDIO_PROVENANCE.md)。
- [Epic Unreal MCP 官方文档](https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor?application_version=5.8)、[MCP Python SDK](https://github.com/modelcontextprotocol/python-sdk)。

# Unreal Engine 工作流与操作

更新：2026-10-22，**v0.4.0-dev / UE 5.8.4**。新增由 Claude Opus 5.5 high 编写的启动/暂停菜单、画面/操控/声音设置和退出确认；修正步行与驾驶的手柄视角方向，提供独立鼠标/手柄反转和灵敏度。设置与显示恢复行为见 [菜单说明](UE_FRONTEND.md)，当前发行验证见 [UE_VERIFICATION.md](UE_VERIFICATION.md)。v0.3 的扫描人物、动画、公共模型和 PBR 材质继续保留，其历史验收见 [v0.3 记录](UE_VERIFICATION_V0_3.md)。物理手柄与混音试听仍需单独验收。

## 位置与工具链

- 工程：`D:\NeonCoastUE\Unreal\NeonCoast.uproject`，分支 `ue5-migration`。
- 原 Godot 工程：`D:\百度招新作业`，保留原版本和历史。旧版测试、截图与四段故事不作为 UE 版的完成证据。
- 引擎：Epic 官方 UE 5.8.4，`D:\Work_software\UnrealEngine\UE_5.8`。
- 编译器：VS 2022 Build Tools 17.14.42，`D:\Work_software\VSBuildTools2022`。当前构建使用 MSVC 14.44 与 Windows SDK 10.0.26100.0。
- 模型准备：Blender 4.5.14 LTS CLI；下载与音频脚本使用本机 Python，贴图均值测量需要 Pillow，音频生成需要 NumPy。
- 原生 MCP：`ModelContextProtocol`、`AllToolsets` 仅在 Editor 目标启用，端口默认为 `http://127.0.0.1:8008/mcp`。客户端使用 `.venv-mcp` 中的官方 Python MCP SDK。

## 构建、运行与 MCP

从 `D:\NeonCoastUE` 执行。源码克隆不会自动包含本机下载的扫描人物和所有生成资产；第一次 Prepare/Package 前先完成素材准备。完整来源、校验与重建说明见 [UE_REALISM.md](UE_REALISM.md)。关闭本工程 UE 编辑器后运行：

```powershell
# 下载/校验公共环境素材和官方扫描人物；复用匹配的现有文件。
python tools/fetch-realism-assets.py
python tools/fetch-scan-characters.py

# Blender 后台准备 15 个公共模型派生副本，原始 glTF 保持不变。
& 'D:\Work_software\Blender\blender-4.5.14-windows-x64\blender.exe' --background --factory-startup --python tools/prepare-public-models.py

# 从本机 Epic 引擎安装复制动画依赖，并测量 PBR 颜色贴图。
python Unreal/Content/Python/build_realistic_characters.py --prepare-template-only
python tools/measure-realism-textures.py

# 初次准备时生成原创 WAV；本机已经生成。需要 host Python 和 NumPy。
python tools/generate-audio.py

# 检查 / 编译 / 编辑器资产生成
powershell -ExecutionPolicy Bypass -File tools/unreal.ps1 -Action Check
powershell -ExecutionPolicy Bypass -File tools/unreal.ps1 -Action Build
powershell -ExecutionPolicy Bypass -File tools/unreal.ps1 -Action Prepare

# 开发用 UE -game 模式；启动UE5.cmd 则优先运行已有独立包
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

默认[启动游戏.cmd](../启动游戏.cmd)优先运行[已打包 Windows 程序](../builds/NeonCoast-UE-Windows/Windows/NeonCoast.exe)，无需打开编辑器；`打开工程.cmd` 指向 UE，`启动旧版Godot.cmd` 保留旧版入口。`Prepare` 及其后的行动会先增量编译，再由 Bootstrap 导入公共模型、生成 PBR/扫描材质、重定向人物动画，并准备音频与入口地图。`Package` 已包含准备阶段，无需重复执行 Prepare。引擎安装在其他位置时传 `-Engine '绝对引擎目录'`，并同步修改人物准备脚本的 `ENGINE` 常量。日志位于 `artifacts/ue-*.log`，工具链检测为 `artifacts/ue-toolchain.json`，打包目录为 `builds/NeonCoast-UE-Windows`。复制游戏时需完整保留其中的 `Windows` 文件夹；开发用 `Play` 仍依赖本机 UE。

`Package` 为每次运行创建工程内专用 UAT 日志目录，并在当前进程的 `NO_PROXY` 中补入 `[::1]`，避免 EFS 日志复制与 Zen 回环代理问题。仅编辑器所需的 `GameFeatureData` 扫描设为 editor-only。下载器与资产生成器校验并复用匹配资源；导入与打包结果分别见本轮 `artifacts/ue-prepare.log`、`artifacts/ue-package.log`，不以旧运行耗时判断新包是否完成。

开发游戏与烟测启动使用 `-DisablePython`，避免运行游戏时初始化仅编辑器需要的 Python 工具类型。资产生成阶段仍启用 Python。`Test` 的自动化报告位于 `artifacts/ue-automation-时间戳/index.json`。发行报告 `artifacts/ue-release-verification.json` 记录真正运行的 EXE 及 Pak/IoStore 内容文件大小、SHA-256 和检查数；`artifacts/ue-release-render-verification.json` 记录独立渲染的运行标识、截图/CSV 哈希及退出状态。应按当前发行文件与运行标识核对证据，不能用目录里保留的历史报告替代。

编辑器启动后，使用实际工具 schema 操作：

```powershell
.\.venv-mcp\Scripts\python.exe tools/ue_mcp.py --output artifacts/ue-mcp-tools.json list
.\.venv-mcp\Scripts\python.exe tools/ue_mcp.py call list_toolsets
.\.venv-mcp\Scripts\python.exe tools/ue_mcp.py call describe_toolset --args-file artifacts/toolset-query.json
```

先读取工具列表，再调用场景、Python 或测试工具；调用顺序执行。端口可通过 `-McpPort 8010` 更改，并同步客户端 URL。根目录 `.mcp.json` 可供 Claude Code 连接。原生 MCP 为实验性功能，本机此前已实际调用 `EditorToolset.EditorAppToolset.StartPIE`、`SlateInspectorToolset.SlateInspectorToolset.PressKey` 和 `EditorToolset.EditorAppToolset.CaptureEditorImage`，并完成 E/F/M/F12 操作。`artifacts/ue-characters-editor.log`、`artifacts/ue-final-editor.log` 以及 `docs/images/ue/first-view.png`、`driving.png`、`map.png` 是 **v0.2 历史证据**。v0.3 历史截图保存在 `docs/images/ue/v0.3-packaged-game.png`；当前 v0.4 菜单和游戏截图、同包验收结果见验证记录。

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
| 画质、操控、音量设置 | Esc → 设置 | Menu → 设置 |
| 窗口 / 无边框全屏 | F11 | — |
| 隐藏 HUD 的照片视图 | F6 | — |
| 保存截图 | F12 | — |

打开地图后可鼠标点击设置标记，Delete 清除标记。地图和帮助会暂停世界。截图写入当前 UE `ProjectSavedDir()/Screenshots`。照片视图仅隐藏 HUD，不是自由飞行摄影机。物理手柄、不同设备布局和实际振动未完成验收。

## 已实现的可玩循环

**驾驶与城市。** 使用 Chaos 四轮车辆、Epic 官方 SportsCar 骨骼/物理/动画资产、转向与刹车、车损、灯光、喇叭和安全出口检测。高速跳车继承速度并产生伤害与短暂恢复。主街区约 500 米宽，包含海滨道路、海滩、酒店、招牌和远景；本轮增加真实立面模块、海岸阔叶树、长椅和路灯，地面与墙面使用 PBR 贴图，保持既有道路、碰撞布局和随机生成顺序。场景按批次生成，支持昼夜变化。初始生成 64 名行人、8 辆行驶交通车和 1 辆玩家附近车辆；警方响应另外生成车辆。

**行动与追捕。** 步行、奔跑、跳跃、肩部瞄准、带枪口遮挡检测的射击、12 发弹匣与有限备弹、换弹、伤害和死亡恢复。行人可问候、受惊逃离和受伤。犯罪提高 0–3 级通缉；警车根据目击和最后报告位置追逐/搜索，持续目击或新犯罪阻止热度衰减。警方集成检查覆盖正常导演派车、真实物理移动、视线更新、遮挡后搜索、废车替补和真实时间消退；本轮实测结果见验证记录，完整城市追逐的手感仍需连续游玩调校。

**NIGHT COURIER。** 出生点附近海岸仓库接受任务，开车到内陆交付点并停下交互，摆脱搜索后返回领取 $750。任务可重复。当前 UE 版是一项三阶段任务，没有宣称迁移完旧 Godot 的四段故事。

**COAST CIRCUIT。** 南侧道路出发，95 秒内按顺序驾驶通过 8 个道路检查点，奖励 $450、记录最佳时间。步行或经过后续检查点不计数。任务期间不能开始比赛；重启后须到起点重新发起比赛。自动检查传送定位各门验证规则，没有证明玩家已驾驶跑完全程或难度已经调校。

**探索和服务。** 8 枚海岸代币需要步行靠近，每枚 $75，全部找到另加一次 $400。车库维修 $100，诊所医疗 $75；没有损伤时不扣款。死亡最多收取 $125，取消当前任务/比赛并回到安全点。

**导航与声音。** 地图、小地图、目标提示、距离、现金、通缉和交互信息来自当前游戏状态；世界标记与金币使用可见网格，支持自定义地图标记、暂停、画质选择和截图。两个原创 16 秒合成器电台只在车内播放，另有速度驱动引擎声、海滨环境、附近警车警笛、喇叭、枪声、换弹和脚步。实机日志确认两个电台与四个循环声实例初始化；实际混音与试听仍待验收。

## 存档与边界

`NeonCoastProgress_v1` 保存现金、代币、完成任务数、比赛胜数/纪录、任务阶段、通缉热度与最后犯罪位置；上一个有效存档保存在 `NeonCoastProgress_v1_backup`。字段不合法时尝试读取备份。开发工程通常位于 `Unreal/Saved/SaveGames`，打包版由 UE 平台存档目录规则决定。不会恢复正在进行的比赛、玩家位置或电台状态。

`-NeonCoastSmokeTest` 禁止读取和写入正式进度，并禁用音频。存档自动测试使用内存序列化；真实磁盘上的损坏主档/备用档恢复、跨进程重开和用户机器权限仍需单独验收。

当前人物为 Renderpeople Eric/Carla 扫描模型，导入高度分别约 186.35/173.07 cm，行人只做少量统一比例变化。Epic 的 Idle、Walk、Jog、Jump 已重定向到两套骨架；Walk 是独立动作，Run 槽使用 Jog，动画原地锁定并按实际速度与角色比例调整步频。首帧日志确认主角加 64 名行人共 65 个扫描角色、无 fallback；最终运行与性能以本轮发行验证为准。当前没有完整瞄准、换弹、死亡和上下车动画，行人和交通规则较简单。没有联机、完整室内系统、船舶/飞机、完整伙伴剧情、GTA VI 原作地图/角色/音乐、完整 City Sample 或 AAA 动画资产。通缉、任务、比赛和城市规模均为本工程原创原型实现，不代表 GTA VI 的具体机制。

扫描人物 FBX、贴图与未烘焙人物 `.uasset` 保持本地，排除在源码提交之外；重新克隆时先运行官方素材下载和生成流程。公共环境源图与衍生模型、Epic 模板依赖也应按 [UE_REALISM.md](UE_REALISM.md) 核对和重建，不能假设本机忽略目录随仓库复制。缺失扫描资产会记录 fallback，不能把回退人物当作写实升级已成功加载。

## 参考、视觉贡献与许可

- 本次资讯依据与未证实内容边界：[GTA_REFERENCE_NOTES.md](GTA_REFERENCE_NOTES.md)。
- **v0.3 写实升级：** Opus 5.5 high 完成视觉方案、PBR/扫描材质和城市视觉代码，并审阅实际渲染；Codex 完成下载核验、模型准备、重定向、技术整合与验收。见 [UE_REALISM.md](UE_REALISM.md)、[本轮调用回执](UE_REALISM_OPUS_RECEIPTS.json)和[视觉方案](UE_REALISM_ART_DIRECTION.json)。作者回执不替代最终运行测试。
- Poly Haven 环境资产采用 CC0，来源与作者见 [PUBLIC_ASSETS.md](licenses/PUBLIC_ASSETS.md)。Renderpeople 扫描模型采用专有免费许可，**不是 CC0/MIT**；Epic 人体动画保留 Epic 条款，来源与原始资产分发限制见 [RENDERPEOPLE.md](licenses/RENDERPEOPLE.md)。统一声明见 [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)。
- Opus 5.5 high 第一轮城市、HUD、材质：[UE_OPUS_RECEIPT.json](UE_OPUS_RECEIPT.json)。
- 第二轮改进城市、昼夜、招牌、地图与 HUD；五个文件写入后会话达到限额：[UE_OPUS_SECOND_PASS_RECEIPT.json](UE_OPUS_SECOND_PASS_RECEIPT.json)。不能把限额退出描述为模型完整验收成功。Codex 负责 API 适配、Chaos 与玩法整合、编译和行为验证。
- **v0.2 历史人物：** 第三轮 Opus 5.5 high 查看 Kenney 人物素材和游戏截图，选定服装、身高及材质：[旧视觉方案](UE_CHARACTER_ART_DIRECTION.json)、[旧回执](UE_CHARACTER_OPUS_RECEIPT.json)。旧模型与动画由 Kenney 创作，采用 CC0；保留来源及导入记录见 [KENNEY_CHARACTERS.md](licenses/KENNEY_CHARACTERS.md)，不是当前扫描人物来源。
- 后续 Opus 5.5 high 完成世界标记与金币，并查看实拍画面调整黄昏阴影可读性：[标记回执](UE_MARKERS_OPUS_RECEIPT.json)、[光照回执](UE_DUSK_OPUS_RECEIPT.json)。
- Epic 车辆资产和参考代码保留第三方许可：[EPIC_VEHICLES_PROVENANCE.md](licenses/EPIC_VEHICLES_PROVENANCE.md)。
- 原创音频与精确资产路径：[AUDIO_PROVENANCE.md](AUDIO_PROVENANCE.md)。
- [Epic Unreal MCP 官方文档](https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor?application_version=5.8)、[MCP Python SDK](https://github.com/modelcontextprotocol/python-sdk)。

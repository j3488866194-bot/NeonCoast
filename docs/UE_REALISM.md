# UE 写实资产升级与本机重建

记录时间：2026-10-22（Asia/Shanghai）。工程为 `Unreal/NeonCoast.uproject`，本机使用 UE 5.8.4。本轮将真实比例人物、公共建筑模块、树木、街道家具和 PBR 贴图接入现有原创城市与玩法。

**当前实现使用生产版 V3 材质及薄地面覆盖层分段，已在 UE 原生渲染中确认海边步道恢复浅米色、草带呈绿色，并保留正常场景阴影。** 最终发行包的玩法、退出、截图和性能结果统一见 [UE_VERIFICATION.md](UE_VERIFICATION.md)，以对应发行文件的哈希为准。编辑器诊断画面与旧版 Kenney 人物包成绩不替代最终独立包验收。

## 资产清单与本地目录

| 来源 | 本轮输入与用途 | 本地位置 / UE 路径 |
| --- | --- | --- |
| Poly Haven，CC0 | `asphalt_02`、`concrete_pavement`、`coast_sand_02`、`white_plaster_02`、`red_brick_plaster_patch_02`、`grass_path_2`；六组 2K 表面颜色、DirectX 法线和粗糙度贴图 | `Unreal/ArtSource/PublicAssets/<asset-id>/textures/` → `/Game/PublicAssets/Textures/` |
| Poly Haven，CC0 | `modular_urban_apartments_facade`；墙、窗门组合、檐口、冠饰与壁柱 | 原始 glTF 和依赖保存在 `Unreal/ArtSource/PublicAssets/`；衍生 GLB 在 `Prepared/`，导入到 `/Game/PublicAssets/Models/` |
| Poly Haven，CC0 | `island_tree_02`、`painted_wooden_bench`、`street_lamp_01` | 同上；分别生成 `SM_CoastalTree`、`SM_Bench`、`SM_StreetLamp` |
| Renderpeople，专有免费许可 | Eric Rigged 001、Carla Rigged 001 的 UE4 骨架 FBX 与原始贴图；Eric 为主角，行人使用两种扫描模型 | `artifacts/realism/renderpeople/eric/`、`carla/` → `/Game/RealisticCharacters/` |
| Epic，UE 模板内容 | Manny/Quinn 共享人体骨架及 Idle、Walk、Jog、Jump，重定向到两个扫描角色 | 本机 UE `Templates/TemplateResources/High/Characters/Content/Mannequins` → `/Game/Characters/Mannequins`；衍生动画在 `/Game/RealisticCharacters/<Name>/Animations/` |

公共资源原始集合为 **54 个文件、165,503,508 字节**；Renderpeople 两个固定 ZIP 合计 **224,279,232 字节**。Epic 动画准备步骤从合法安装的本机引擎复制 **27 个依赖文件、84,756,481 字节**，不会从第三方下载 Epic 内容。

公共模型准备器生成并导入 15 个网格；当前城市实际使用其中 9 种：`SM_FacadeWindowPanel`、`SM_FacadeDoorPanel`、`SM_FacadeWall`、`SM_FacadeCornice`、`SM_FacadeCrown`、`SM_FacadePier`、`SM_CoastalTree`、`SM_Bench`、`SM_StreetLamp`。其余模块保留供后续使用。`island_tree_02` 是海岸阔叶树，不是棕榈。

六组表面贴图生成 16 个 `MI_CoastPBR_*` 实例，覆盖道路、人行道、混凝土、步道、草地、干湿沙滩、七种建筑配色、灰泥和砖墙。平铺尺寸采用来源元数据的实测物理尺寸；线性颜色均值由全部像素测量，作为材质染色计算输入。模型保留原 UV 与材质槽，扫描人物使用其原始贴图图集。

这些源图、FBX、glTF/GLB、导入网格和验证报告是本机获取或生成的资源，不能把当前磁盘内容理解为源码克隆自带的完整素材包。尤其 `artifacts/` 和 `Unreal/Content/RealisticCharacters/` 已由 `.gitignore` 排除；克隆后需自行按下述流程获取许可资产并生成。Epic 依赖另需本机有效引擎安装。

## Opus 与技术整合范围

**Claude Opus 5.5 high** 通过实际 Claude Code CLI 完成六轮工作：[写实视觉方案](UE_REALISM_ART_DIRECTION.json)、PBR/扫描人物材质构建、城市公共模型与光照、[首次视觉复查](UE_REALISM_VISUAL_REVIEW.json)、[恢复后的视觉复查](UE_REALISM_RESUME_REVIEW.json)，以及目标标记收尾。标记调整为更薄的琥珀色。完整模型、会话、耗时、作者输出哈希和作用范围见[六轮调用回执](UE_REALISM_OPUS_RECEIPTS.json)。

两次复查均基于各自列出的实际静态预览。回执证明调用与写入范围；复查中的色值为目测估计，技术假设和建议不作为已修复或最终发行包验收的证据。`tools/measure-ue-render.py` 只统计帧时间，不测量曝光或 sRGB 色值。

Codex 负责官方来源与哈希核验、Blender/UE 导入、动画重定向、C++ API 适配、碰撞/原点审查、材质与地面几何诊断、编译和运行验证。人物模型来自 Renderpeople，动画来自 Epic；没有将第三方扫描或动画署名为 AI 原创。

## 生产材质与海边步道修正

`build_realism_materials.py` 的默认 `build()` 生成并复用生产版 GroundV3/WallV3，将 16 个材质实例指向对应生产父材质；常规 Prepare/Package 不再恢复 v1。V3 直接输出世界空间法线，水平地面采用固定朝上的法线基底与 WorldXY 贴图投影；Concrete/WetSand 仍使用适合竖直或倾斜表面的箱体投影。早期 Sidewalk-only V2 只用于诊断，不是发行配置。

法线缓冲区恢复正确后，出生点海边仍存在暗棕色长条。因此还对超长、无碰撞、厚度不超过 4 cm 的 Sidewalk/Promenade/Lawn/Asphalt 视觉覆盖层，沿较长轴拆为不超过 20 m 的段。各段保持原高度、覆盖范围和世界坐标贴图，原碰撞地面不参加分段。该修改后，原生正常光照画面中的步道变为浅米色铺地、草带恢复绿色，人物及建筑阴影仍可见；没有通过关闭阴影来获得该结果。

诊断记录为 `artifacts/realism/diagnostics.json`，运行 ID `263b759a8c2b46459a599f5c306426db`，退出码 0；其 `diagnostic-fullres.png` SHA-256 为 `4fb30303fbe0cf146639e9ce99c357d7486e97e4651fcb2551f2544a2cbbadf9`。这证明该诊断帧的表现，不扩展为全城、全天候或最终包稳定性结论；发行结果仍以 [UE_VERIFICATION.md](UE_VERIFICATION.md) 为准。

## 本机重建命令

以下命令针对 `D:\NeonCoastUE`。需要 Windows C++ 构建工具、UE 5.8、Blender 4.5 LTS，以及带 Pillow 的 Python。已核对本机普通 `python` 为 Python 3.10，含 Pillow 12.2.0；`.venv-mcp` 不含 Pillow，不能直接用它执行贴图均值测量。本机 Blender 路径如下。开始前关闭本工程的 UE 编辑器，避免模板复制、资产导入和编译争用。

```powershell
Set-Location 'D:\NeonCoastUE'
$realismBlender = 'D:\Work_software\Blender\blender-4.5.14-windows-x64\blender.exe'

# 1. 下载或校验固定的 Poly Haven 原始集合，保留 glTF 相对依赖。
python tools/fetch-realism-assets.py

# 2. 使用 Blender 后台处理派生副本，不修改原始文件。
& $realismBlender --background --factory-startup --python tools/prepare-public-models.py

# 3. 下载或复用两个经过 SHA-256 固定的官方人物 ZIP，校验 CRC 后解压。
python tools/fetch-scan-characters.py

# 4. 从本机 Epic 引擎安装复制动画/骨架的依赖闭包；此步不启动 UE。
python Unreal/Content/Python/build_realistic_characters.py --prepare-template-only

# 5. 测量六组颜色贴图的线性均值，供 PBR 材质构建器读取。
python tools/measure-realism-textures.py

# 基础工程首次重建还需要原创音频源；本机已存在，重复执行可重新生成。
python tools/generate-audio.py

# 6. 编译并运行 UE 资产生成；失败时先检查日志，不继续打包。
powershell -ExecutionPolicy Bypass -File tools/unreal.ps1 -Action Prepare

# 7. 编译、准备资产、Cook、Stage 并打包到 builds/NeonCoast-UE-Windows。
powershell -ExecutionPolicy Bypass -File tools/unreal.ps1 -Action Package
```

`Package` 自身会再次执行编译和 `bootstrap_project.py`，因此已确认准备结果时可以省略独立的 `Prepare`。Bootstrap 依次执行公共模型导入/材质整理、PBR 构建、扫描人物导入与动画重定向、扫描材质构建和最终骨架资源整理；运行游戏不需要 Python。现有基础游戏资产与旧人物导入仍由该工程原有流程管理，以上命令针对本机完整工程的写实层重建。

`build_realistic_characters.py` 当前按本机 `D:\Work_software\UnrealEngine\UE_5.8` 寻找模板；迁移到其他引擎目录时需同步调整该脚本的 `ENGINE` 常量。`unreal.ps1` 另外支持 `-Engine <安装目录>`。不能仅修改 PowerShell 参数而假设 Python 模板路径也随之改变。

已有本地缓存时可只做离线验证：

```powershell
python tools/fetch-realism-assets.py --verify-only
python tools/fetch-scan-characters.py --offline
```

扫描下载器的本地复用已实际验证：两个 ZIP 的固定大小/SHA-256、全量 ZIP CRC 和 26 个解压文件的 CRC 均匹配，下载 0 字节、模型重写 0 个文件。首次联网下载分支未在该脚本交付后重跑；它只允许 HTTPS `renderpeople.com`，在跟随跳转前检查域名，并拒绝 ZIP 路径越界和文件哈希变化。

## 本轮已验证事实与边界

| 项目 | 已有证据 | 尚不代表 |
| --- | --- | --- |
| C++ | 本轮城市公共模型及角色集成已通过实际编译 | 最终发行包全部测试通过 |
| 公共模型 | `Unreal/Saved/PublicModelImportReport.json` 记录 15 个网格导入；首帧日志载入 9/9 个城市模型 | 所有路线、遮挡和距离下的视觉验收 |
| PBR 材质 | `Unreal/Saved/RealismMaterialReport.json` 记录生产 V3 材质生成；原生诊断记录法线及正常光照结果 | 所有光照时刻均达到视觉方案目标 |
| 扫描人物 | Eric 导入高度 186.35 cm、Carla 173.07 cm，各 95 根骨骼；首帧日志有 65 条角色成功配置、0 条 fallback | 每种战斗、跳车或动画过渡均已验收 |
| 动画 | 两人各有 Idle/Walk/Run/Jump；逐段 21 次姿态采样的原地 root 平移范围为 0 cm。Run 使用 Epic Jog 源；CharacterMovement 管理角色位移，原始 root 轨迹用于校准步频 | 足滑小于特定阈值、完整瞄准/换弹/死亡动画或长时间表现 |
| 技术定位修正 | 树模型以 `NCPlacementBase` 对齐实际树脚；长椅根据真实正面转为 yaw −90°；树冠约束优先于最低高度 | 所有环境道具已做逐个交互测试 |
| 地面修正诊断 | V3 与分段覆盖层的实际画面中，浅米色铺地、绿色草带和场景阴影可见；诊断退出码 0 | 全城光照、最终发行包帧率、稳定性或完整玩法通过 |

下图为独立 Windows 游戏截图，具体版本、运行 ID、截图哈希及采样条件见 [UE_VERIFICATION.md](UE_VERIFICATION.md)。该发行证据与编辑器诊断记录分别保存。

![独立 Windows 游戏实际画面](images/ue/packaged-game.png)

**历史预览，仅用于溯源：** [首次预览](../artifacts/realism/preview.png)对应 `artifacts/realism/preview.json`，运行 ID `78ada46a5b9d42c4acf27b98eb6fdeb3`，截图 SHA-256 `075de2bfddd89e26326bf9ca5e1df4c5b18665d261539f9f14fc2108dfb1cb77`。该图由编辑器程序的游戏模式渲染，退出码 0，早于 V3 和地面分段修正；它是首次 Opus 复查输入，不是当前发行画面或性能样本。V2 对比图 `artifacts/realism/preview-xy.png` 也仅是中途诊断。

道路、人行道和原建筑碰撞布局保持；替代树木继续消费原随机数调用，新增视觉变化使用独立随机流，避免建筑与路线随素材是否存在而改变。扫描人物正常加载时替换旧版卡通人物；缺少对应资源时会明确记录 fallback。部分海滩道具、建筑主体、车辆和玩法仍沿用既有原型内容，本轮不表示已完成 AAA 城市或完整 GTA VI 功能。

## 许可与溯源

- [PUBLIC_ASSETS.md](licenses/PUBLIC_ASSETS.md)：Poly Haven 资产作者、官方 URL、CC0、物理尺寸和校验清单。原始下载清单位于 `Unreal/ArtSource/PublicAssets/manifest.json`，派生模型清单位于其 `Prepared/prepare_report.json`。
- [RENDERPEOPLE.md](licenses/RENDERPEOPLE.md)：Eric/Carla 官方下载与哈希、Renderpeople 专有免费许可、Epic 动画来源。免费不等于 CC0/MIT；原始 FBX、纹理和可编辑扫描 `.uasset` 不作为独立资产库公开分发，游戏仅按相应条款集成 cooked 资源。
- [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)：项目统一第三方声明。Epic 人物模板与车辆资产保留各自许可，车辆的具体来源另见 [EPIC_VEHICLES_PROVENANCE.md](licenses/EPIC_VEHICLES_PROVENANCE.md)。

这些文档与下载脚本不授予新许可。源素材和衍生素材继续受各自条款约束；源码仓库的 MIT 许可不会替代它们。

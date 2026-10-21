# UE 版验证记录

日期：2026-10-21。工程 `Unreal/NeonCoast.uproject`，UE 5.8.4。本文只记录 UE 版；Godot 的 213 项测试与旧截图见 [VERIFICATION.md](VERIFICATION.md)，不计入这里。

## 当前结果

Windows 独立包已通过 **117 项真实世界检查：95 项玩法、22 项警方集成，0 项失败**；另有 2 项存档自动化测试通过。普通 DX12 游戏实际启动后正常关闭，退出码 0、无引擎错误。同一发行包的 1600 × 900 原生截图和 720 帧独立渲染检查也通过，人物、行人、车轮、标记和 HUD 已目视确认。

可双击[启动游戏.cmd](../启动游戏.cmd)或[独立包启动入口](../builds/NeonCoast-UE-Windows/Windows/NeonCoast.exe)，无需打开编辑器。复制游戏时须完整保留 `Windows` 文件夹。

| 项目 | 结果 | 本地证据 |
| --- | --- | --- |
| 当前 Editor C++ 构建 | 成功；新版人物与警察代码已实际运行 | `artifacts/ue-build.log`、`artifacts/ue-final-build-console.log` |
| 真实 UE 世界玩法检查 | **95 PASS / 0 FAIL**，世界运行 58.99 秒 | `artifacts/ue-smoke.log`，`NEON_COAST_SMOKE_PASS` |
| 正常警方导演集成检查 | **22 PASS / 0 FAIL**，世界运行 32.46 秒 | `artifacts/ue-police.log`，`NEON_COAST_POLICE_PASS` |
| 存档自动化测试 | **2 Success / 0 Failed / 0 warnings** | `artifacts/ue-automation/index.json` |
| CC0 骨骼人物 | 11 个素材资产、59 根骨骼；主角与 64 名行人已运行 | `artifacts/ue-characters-import.log`、`Unreal/Saved/CharacterImportReport.json`、`artifacts/ue-characters-editor.log` |
| 独立 DX12 渲染 | **720 帧运行，第 600 帧原生截图，ExitCode 0 / engine_failures 0 / audio_ready true**；无编辑器、无 NullRHI | `artifacts/ue-release-render-verification.json`、`docs/images/ue/packaged-game.png` |
| 画面与交互 | 独立图的人物朝向、脚底、行人、车轮、标记与 HUD 正常；编辑器另验驾驶和地图 | 下方图片、`artifacts/ue-final-editor.log` |
| 原生 MCP 操作 | StartPIE、CaptureEditorImage、E 接任务、F 上车、M 地图、F12 截图成功 | `artifacts/ue-characters-editor.log`、`artifacts/ue-final-editor.log`、`artifacts/mcp-final-key-*.json` |
| 音频 | 9 个源 WAV 校验通过，实机初始化 2 个电台与 4 个循环声；混音试听未验收 | `artifacts/audio-src/manifest.json`、`NC_AUDIO_READY radioTracks=2 loops=4` |
| Windows 独立包 | **117 PASS / 0 FAIL**；Cook 36.69 s、0 errors / 0 warnings，BuildCookRun 83.82 s、退出码 0 | `artifacts/ue-package.log`、`artifacts/ue-release-verification.json` |
| 普通独立 DX12 游戏退出 | **ExitCode 0 / engine_failures 0 / audio_ready true**；音频就绪后运行 10 秒，关闭本游戏窗口 | `artifacts/ue-release-normal-exit-verification.json`，2026-10-21 23:13:00 +08:00 |
| 导入器重复运行 | **修复已验证**：三个现有材质成功复用，完整资产准备与最终打包通过 | `artifacts/ue-prepare.log`，三条 `NEON_COAST_CHARACTER_MATERIAL_REUSED` 与 `NEON_COAST_ASSETS_READY` |

日志和大型构建产物位于忽略提交的 `artifacts/`、`builds/`，是本机证据；克隆源码不会自动取得这些文件。

## 当前实机画面

主图为 **1600 × 900 独立 Windows DX12 游戏**，包含 Kenney 白西装主角与行人、修正后的车轮、Opus 世界标记/金币及黄昏光照。图像来自渲染检查运行 `1599d67cc26e44f1a7d3b8100c3c8d33`，检查于 **23:14:29 +08:00** 完成；该运行的退出码和引擎错误数均为 0。

![独立 Windows 游戏实际画面](images/ue/packaged-game.png)

以下为同版编辑器中的交互验证截图；另有[步行视角](images/ue/first-view.png)。

![编辑器交互验证：驾驶视角](images/ue/driving.png)
![编辑器交互验证：城市地图](images/ue/map.png)

日志记录 65 个 `NEON_COAST_CHARACTER_VISUAL` 实例和真实任务开始。运行中通过 MCP 按键完成交互与截图；静态图片不证明所有动画过渡、驾驶路线和长期帧率均已验收。

## 有渲染性能抽样

`artifacts/ue-render-performance.json` 对应上述同一运行与 CSV 哈希。条件为本机 **1600 × 900、DX12、High、60 FPS 上限、静止出生点镜头**；取预热后的零基帧 240–598，共 359 帧，未计入第 600 帧截图开销。

| 指标 | 平均值 | P95 |
| --- | ---: | ---: |
| FrameTime | 16.668 ms | 17.446 ms |
| GameThreadTime | 5.021 ms | 5.626 ms |
| RenderThreadTime | 6.076 ms | 6.741 ms |
| GPUTime | 5.800 ms | 6.146 ms |

按平均帧时间估计为 **60.0 FPS**，这是有限帧设置下的静止场景短样本，不是无上限性能、驾驶性能或长时间稳定性基准。截图 SHA-256 为 `86706c0e7176948f52b1cbaf87640ca02bd25714c4c7e61b6a4551a566a584ab`；CSV SHA-256 为 `82491294bd688d73b3dee14482abd4b940750ee18fcc8f3edd48826c0ecaaff0`。

## 行为覆盖与测量

- 初始玩家落地、至少 9 辆基础车和 64 名行人存在；4 秒观察中 **55 / 64** 名行人移动超过 50 cm。
- 真实 Chaos 刚体、4 个车轮状态、至少 3 轮接地；通过真实 Tick 加速、行驶与高速跳车，继承速度、扣血、恢复、落地和跳跃。
- 瞄准、扣弹、有限备弹换弹、NPC 受击/受惊及犯罪热度；肩部相机能见目标而枪口被挡时不能穿墙命中。
- 医疗扣 $75、健康状态不重复收费；死亡约 3 秒恢复、收费一次、取消任务并清除通缉。
- 三阶段快递任务的接取、驾车交付、拒绝步行/远程交互、真实时间搜索、返回和 **$750** 结算；本次搜索 **26.64 s**。
- 街道赛 8 门顺序、拒绝跳门/步行、完成 **$450** 与最佳时间记录；8 枚代币各 $75、全收集额外 $400、避免重复奖励。
- 已赚取进度经过 SaveGame 内存序列化往返；测试禁用正式玩家存档。

| 本次源码烟测读数 | 结果 |
| --- | ---: |
| 平路峰值速度 / 加速段位移 | 74.15 km/h / 30.2470 m |
| 稳定停放车体 Z 范围 | 0.000 cm（日志舍入） |
| 驾驶车体 Z 范围 / 最大相邻帧竖直位移 | 0.041 cm / 0.0004 cm |
| 跳车初始速度 / 跳跃高度 | 13.7236 m/s / 1.5999 m |
| 世界运行时间 | 58.99 s |

这些为约 60 FPS、平坦短直线的读数，不是复杂碰撞或所有帧率下的最差值，也不证明摄像机在任意路线绝无抖动。

**警方单独启用正常导演实测。** 犯罪报告触发一星派车、生成两辆健康警车；警方自然物理行驶 **53.7072 m**。验证道路视线重获和最后位置更新、可见目标持续追踪、废车不占响应名额且生成新的替补车。真实碰撞遮挡后失去视线，隐藏移动不更新警方最后位置；警方继续 Tick，搜索延迟与热度自然衰减 **19.45 s** 后清除通缉。22 项全部通过，完整城市追逐手感仍需连续游玩。

## 资产、Opus 与构建修复

Opus 5.5 high 的前两轮贡献城市、HUD 和材质；第三轮查看真实图片后确定人物服装、178 cm 身高和哑光材质，后续又完成网格标记/金币及黄昏阴影可读性。回执：[城市第一轮](UE_OPUS_RECEIPT.json)、[第二轮](UE_OPUS_SECOND_PASS_RECEIPT.json)、[人物方案](UE_CHARACTER_OPUS_RECEIPT.json)、[世界标记](UE_MARKERS_OPUS_RECEIPT.json)、[黄昏光照](UE_DUSK_OPUS_RECEIPT.json)。第二轮在写入五个目标文件后达到会话限额；其余回执与后续运行证据分别记录实际完成范围。Codex 负责 API 适配、玩法整合、导入、编译和验证。

Kenney CC0 资产包含一份网格/共享骨架、三段动画、三张纹理和三个材质。模型实测高 376.468611 cm，缩放 0.472814984 对应 178 cm；Idle 1.066667 s、Run 0.666667 s、Jump 0.5 s，排除了单帧 targeting pose。UE 成功重建原始 FBX bind pose，缺少 bind-pose 骨骼和 smoothing groups 的警告已记录；当前画面已确认基本站姿、朝向和脚底对齐。没有独立 walk、aim、reload、death 动画。来源与哈希见 [人物许可](licenses/KENNEY_CHARACTERS.md)。

| 影响 | 解决与验证边界 |
| --- | --- |
| 并行打包/着色器编译导致内存提交不足，首次渲染崩溃 | 降低 shader worker 与 PSO 并发并错开任务；后续多轮实际渲染成功 |
| AppData UAT 日志 EFS 加密阻止 Stage 到 D:；Zen IPv6 回环受代理影响 | 为每次构建创建 D: 专用日志目录，当前进程 `NO_PROXY` 加 `[::1]`；最终 BuildCookRun 已 `BUILD SUCCESSFUL` |
| 重复导入删除被 C++ 硬引用的材质节点，触发 `!IsRooted()` | 已改为校验并复用已有图，仅新材质创建节点；记录设计 SHA，变化时明确拒绝无声覆盖；Prepare 复用三材质成功，最终打包通过 |
| 打包版扫描仅编辑器提供的 `GameFeatureData` 类，产生启动 ensure | 该扫描项已设为 editor-only，重新打包；普通 DX12 启动和关闭均无引擎错误 |

## 独立包快照与剩余范围

发行检查由 `tools/verify-ue-release.ps1` 完成，最终报告 `artifacts/ue-release-verification.json` 的状态为 `passed`，检测时间 **2026-10-21 23:16:08 +08:00**，运行标识 `de55f771897242a5b6666978215fb8f8`。其 EXE 与全部五个 Pak/IoStore 文件哈希，与 **23:14:29** 已通过的独立渲染报告一致；行为、图像和性能记录对应同一包内容。

- **95 项玩法 + 22 项警方 = 117 项全部通过，0 项失败**。
- 引擎错误数为 0；玩法世界运行 59.00 s，警方世界运行 32.45 s，警方自然行驶 53.7102 m，遮挡搜索 19.45 s。
- 实际游戏 EXE 为 332,877,312 字节；SHA-256：`e90aff0262fdb45b6f59f870cecb341299b05b0b22397feaf6408ce6f72b4da0`。
- `NeonCoast-Windows.pak` SHA-256：`b412f0e8e5016f3e7de41f06069f6e80111eabd17f2432a8a487b19482c5ca19`；其余 `.ucas/.utoc` 的完整大小和哈希保存在两份 JSON 报告。
- 模式：独立 Win64 Development、NullRHI、真实世界 Tick、禁用玩家存档，不依赖编辑器或 MCP。
- 骨骼人物日志共 66 条，包含烟测额外生成的一个 NPC；正常初始人数仍是主角加 64 名行人，共 65 人。没有退回占位几何人物。

被哈希和测试的[实际游戏程序](../builds/NeonCoast-UE-Windows/Windows/NeonCoast/Binaries/Win64/NeonCoast.exe)位于同包子目录。渲染基准工具单独启用同步 CSV 处理（`-csvNoProcessingThread`），绕过本机异步 CSV 后台线程在结束时的异常；正式玩家启动不启用 CSV，不依赖这个测试开关。普通启动与基准运行的退出结果分别验证，不用已生成图片替代正常退出证据。

玩法烟测会将车/人定位到任务点、比赛门和代币点，正常计时与 Tick 仍运行。**约 3.9 s 的比赛纪录来自传送夹具，不是玩家驾驶一圈成绩**。警方夹具使用真实碰撞遮挡与受控目标位置，不能代替完整城市追逐调校。

存档测试为 `NeonCoast.Progress.SaveRoundTrip` 和 `NeonCoast.Progress.RejectInvalidSave`，覆盖内存序列化与无效数据拒绝；没有覆盖磁盘损坏、权限、备用存档跨进程恢复或真实玩家重启读档。音频初始化与源 WAV 校验不能代替实际听觉验收。物理手柄、焦点/音频设备切换、多分辨率和长时间游玩尚无完整验收结论。

参考资料见 [GTA VI 官方研究](GTA_REFERENCE_NOTES.md)；其他来源见 [Epic 车辆](licenses/EPIC_VEHICLES_PROVENANCE.md)、[原创音频](AUDIO_PROVENANCE.md)。本工程不含 Rockstar 代码、人物、地图、音乐或泄露素材。

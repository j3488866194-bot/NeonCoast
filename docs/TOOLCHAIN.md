# 可复核的工具链记录

- 游戏引擎：Godot `4.7.2.stable.official.ed1daf0bf`，Windows x86_64，OpenGL Compatibility 渲染。
- 官方来源：https://github.com/godotengine/godot-builds/releases/tag/4.7.2-stable
- 安装位置：`D:\Work_software\Godot\4.7.2`。
- 已启用 Godot 官方 self-contained 模式，编辑器配置和导出模板存放在引擎目录下的 `editor_data`。原因是本机原有 AppData 编辑器设置文件写回失败；使用独立配置后，工程导入退出码 0 且没有该错误。原有配置和权限未更改。官方说明：https://docs.godotengine.org/en/4.7/classes/class_editorpaths.html
- Windows 引擎 ZIP 已与同一官方发布中的 `SHA512-SUMS.txt` 核对，摘要：`83decd58fdf67b9d657958a1ae6bf1929c20785315a81effe245874cdc57acb709bf868e00778a96984338c1b29dafdb453c6847747694621c6ecf5da2259993`。
- 导出模板整体包也经过官方 SHA-512 核对，仅解出本机需要的 Windows x86_64 release/debug 模板及版本文件。
- Claude Code：`2.1.306`，官方原生安装，Windows Authenticode 签名有效，签名者为 Anthropic, PBC。
- 登录复用本机已有有效授权，未在聊天或源码中保存、要求或公开密码、API Key 或登录凭据。
- Opus 连接检查实际返回 `CONNECTION_OK`；返回元数据的 `modelUsage` 与 `canonicalModel` 均为 `claude-opus-5-5`。
- 界面任务固定 `--model claude-opus-5-5`，最初以 max 档执行。该次持续推演但未写出界面，终止该工作进程后，按用户同时允许的 high 档重新执行，范围仍为 HUD/UI。输入任务见 `CLAUDE_UI_TASK.md`，设计接口见 `UI_DESIGN.md`。两次执行日志分别保存在忽略提交的 `artifacts/claude-ui.jsonl` 和 `artifacts/claude-ui-high.jsonl`。
- 本地 Git 使用原有用户身份配置，未修改全局 Git 身份。尚未设置远程仓库或自动公开发布。

## Claude 实际交付边界

全量 HUD 的 max/high 请求未得到可用实现：一次限定输出的请求返回了明确的输出上限错误，另一轮精简请求生成了重复内容，均未作为游戏代码采用。最终地图模块由独立的 Opus 5.5 high 请求生成，结果 `is_error=false`、`num_turns=1`，`modelUsage` / `canonicalModel` 为 `claude-opus-5-5`，provider 为 `firstParty`。日志为忽略提交的 `artifacts/claude-map-final.jsonl`，生成源码保存在 `scripts/ui_map.gd`；Codex 随后修正了任务点和自定义导航点的区分。

HUD、菜单及剩余界面由 Codex 协作任务完成。不会把失败请求或模拟工具输出说成 Claude 已完成的界面，也不会把工具安装成功当作游戏验证结果。

界面、场景、控制与成品的最终测试结果见 `VERIFICATION.md`。工具安装与一次模型连接成功不代表游戏画面或完整体验已通过验证；两者分别记录。

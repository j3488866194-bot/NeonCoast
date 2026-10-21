extends CanvasLayer
## Native cinematic interface. The actual map is Claude's independent ui_map.gd.

signal action_requested(action: String)
signal setting_changed(key: String, value: float)
signal waypoint_selected(world_position: Vector3)

const MapView = preload("res://scripts/ui_map.gd")
const CREAM: Color = Color("fff2d6")
const CORAL: Color = Color("ff755f")
const TEAL: Color = Color("6ee6ca")
const MUTED: Color = Color("afbfbd")
const INK: Color = Color("101d28")

class Backdrop extends Control:
	var ui: Node
	func _draw() -> void:
		if is_instance_valid(ui):
			ui._paint_backdrop(self)

var current_mode: String = "title"
var current_state: Dictionary = {}
var map_blocks: Array = []
var _root: Control
var _backdrop: Control
var _screens: Dictionary = {}
var _first_focus: Dictionary = {}
var _font: Font
var _display_font: Font
var _continue: Button
var _minimap: Control
var _fullmap: Control
var _mission_title: Label
var _objective: Label
var _mission_distance: Label
var _cash: Label
var _stars: Label
var _health: ProgressBar
var _heat: ProgressBar
var _district: Label
var _clock_label: Label
var _radio: Label
var _speed: Label
var _speed_card: Control
var _car_health: ProgressBar
var _prompt: Label
var _prompt_card: Control
var _map_objective: Label
var _map_distance: Label
var _map_stats: Label
var _toast: Control
var _toast_title: Label
var _toast_subtitle: Label
var _toast_time: float = 0.0
var _completion: Control
var _completion_title: Label
var _completion_reward: Label
var _completion_time: float = 0.0
var _help: Control
var _help_visible: bool = false
var _help_return_focus: Control
var _volume: HSlider
var _sensitivity: HSlider
var _volume_value: Label
var _sensitivity_value: Label
var _quality: OptionButton
var _invert: CheckButton
var _reset_dialog: ConfirmationDialog


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	layer = 10
	_font = _load_font("res://assets/fonts/NotoSansSC.ttf")
	var body_font: FontVariation = FontVariation.new()
	body_font.base_font = _font
	body_font.variation_opentype = {TextServerManager.get_primary_interface().name_to_tag("wght"): 500.0}
	_font = body_font
	_display_font = _load_font("res://assets/fonts/BarlowCondensed-Bold.ttf")
	_root = Control.new()
	_root.name = "Interface"
	_root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_root)
	var theme: Theme = Theme.new()
	theme.default_font = _font
	theme.default_font_size = 20
	_root.theme = theme
	_backdrop = Backdrop.new()
	_backdrop.ui = self
	_backdrop.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_backdrop.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_root.add_child(_backdrop)
	_build_title()
	_build_playing()
	_build_pause()
	_build_settings()
	_build_map()
	_build_photo()
	_build_help()
	_build_notifications()
	_reset_dialog = ConfirmationDialog.new()
	_reset_dialog.title = "清除本机存档"
	_reset_dialog.dialog_text = "这会删除本机的游戏进度。确定清除存档？"
	_reset_dialog.ok_button_text = "清除存档"
	_reset_dialog.cancel_button_text = "保留存档"
	_reset_dialog.confirmed.connect(func() -> void: action_requested.emit("reset_save"))
	_root.add_child(_reset_dialog)
	set_mode(current_mode)


func _load_font(path: String) -> Font:
	if ResourceLoader.exists(path):
		return load(path) as Font
	var fallback: SystemFont = SystemFont.new()
	fallback.font_names = PackedStringArray(["Microsoft YaHei", "Noto Sans CJK SC", "sans-serif"])
	return fallback


func _screen(name_text: String) -> Control:
	var control: Control = Control.new()
	control.name = name_text.capitalize() + "Screen"
	control.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	control.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_root.add_child(control)
	_screens[name_text] = control
	return control


func _style(fill: Color, border: Color = Color.TRANSPARENT, radius: int = 10) -> StyleBoxFlat:
	var box: StyleBoxFlat = StyleBoxFlat.new()
	box.bg_color = fill
	box.border_color = border
	box.set_border_width_all(1 if border.a > 0.0 else 0)
	box.set_corner_radius_all(radius)
	box.content_margin_left = 20
	box.content_margin_right = 20
	box.content_margin_top = 8
	box.content_margin_bottom = 8
	return box


func _panel(parent: Node, at: Vector2, dimensions: Vector2, fill: Color = Color(0.04, 0.09, 0.12, 0.88)) -> Panel:
	var panel: Panel = Panel.new()
	panel.position = at
	panel.size = dimensions
	panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	panel.add_theme_stylebox_override("panel", _style(fill, Color(CREAM, 0.12)))
	parent.add_child(panel)
	return panel


func _label(parent: Node, text: String, at: Vector2, dimensions: Vector2, font_size: int = 20, color: Color = CREAM, display: bool = false) -> Label:
	var label: Label = Label.new()
	label.text = text
	label.position = at
	label.size = dimensions
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	label.add_theme_font_override("font", _display_font if display else _font)
	label.add_theme_font_size_override("font_size", font_size)
	label.add_theme_color_override("font_color", color)
	label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	parent.add_child(label)
	return label


func _button(parent: Node, text: String, action: String, at: Vector2, dimensions: Vector2, primary: bool = false) -> Button:
	var button: Button = Button.new()
	button.text = text
	button.name = action.capitalize().replace(" ", "") + "Button"
	button.set_meta("action", action)
	button.position = at
	button.size = dimensions
	button.focus_mode = Control.FOCUS_ALL
	button.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
	button.alignment = HORIZONTAL_ALIGNMENT_LEFT
	button.add_theme_font_size_override("font_size", 21)
	button.add_theme_color_override("font_color", INK if primary else CREAM)
	button.add_theme_color_override("font_hover_color", INK)
	button.add_theme_color_override("font_pressed_color", INK)
	button.add_theme_color_override("font_focus_color", INK)
	button.add_theme_color_override("font_disabled_color", Color(MUTED, 0.35))
	button.add_theme_stylebox_override("normal", _style(CORAL if primary else Color(0.06, 0.12, 0.15, 0.87), Color(CREAM, 0.15)))
	button.add_theme_stylebox_override("hover", _style(CREAM, CREAM))
	button.add_theme_stylebox_override("pressed", _style(TEAL, TEAL))
	button.add_theme_stylebox_override("focus", _style(TEAL, CREAM))
	button.add_theme_stylebox_override("disabled", _style(Color(0.04, 0.08, 0.10, 0.7), Color(CREAM, 0.05)))
	button.pressed.connect(_button_action.bind(action))
	parent.add_child(button)
	return button


func _button_action(action: String) -> void:
	if action == "controls":
		_show_help()
	elif action == "help_back":
		_hide_help()
	elif action == "reset_confirm":
		_reset_dialog.popup_centered(Vector2i(580, 210))
	else:
		action_requested.emit(action)


func _build_title() -> void:
	var screen: Control = _screen("title")
	_label(screen, "N C  /  ORIGINAL OPEN WORLD", Vector2(82, 46), Vector2(540, 38), 18, TEAL, true)
	_label(screen, "NEON", Vector2(72, 102), Vector2(690, 183), 186, CREAM, true)
	_label(screen, "COAST", Vector2(72, 266), Vector2(690, 183), 186, CORAL, true)
	_label(screen, "霓虹海岸", Vector2(83, 460), Vector2(360, 42), 31, CREAM)
	_label(screen, "日落之后，城市才刚醒来。", Vector2(85, 506), Vector2(450, 36), 19, MUTED)
	_first_focus.title = _button(screen, "开始旅程     /     START", "start", Vector2(84, 570), Vector2(372, 56), true)
	_continue = _button(screen, "继续旅程     /     CONTINUE", "continue", Vector2(84, 638), Vector2(372, 48))
	_button(screen, "游戏设置", "settings", Vector2(84, 698), Vector2(176, 48))
	_button(screen, "操作指南", "controls", Vector2(280, 698), Vector2(176, 48))
	_button(screen, "离开海岸", "quit", Vector2(84, 758), Vector2(372, 46))
	_label(screen, "SINGLE PLAYER  /  自由驾驶 · 城市故事 · 海岸漫游", Vector2(84, 845), Vector2(1050, 30), 16, MUTED)
	var chapter: Panel = _panel(screen, Vector2(1214, 92), Vector2(300, 210), Color(0.04, 0.09, 0.12, 0.55))
	_label(chapter, "01  /  THE COAST", Vector2(24, 18), Vector2(258, 35), 23, TEAL, true)
	_label(chapter, "追着落日\n驶入新的故事。", Vector2(24, 63), Vector2(260, 84), 26)
	_label(chapter, "PALMS. ASPHALT. AFTER HOURS.", Vector2(24, 160), Vector2(260, 24), 16, MUTED, true)
	_label(screen, "西岸公路  /  17:36\n让下一段旅程，由你决定。", Vector2(1160, 756), Vector2(350, 68), 19, CREAM).horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT


func _build_playing() -> void:
	var screen: Control = _screen("playing")
	var mission: Panel = _panel(screen, Vector2(32, 32), Vector2(424, 154))
	_label(mission, "CITY STORIES  /  城市故事", Vector2(18, 9), Vector2(382, 26), 16, TEAL, true)
	_mission_title = _label(mission, "新的开始", Vector2(18, 39), Vector2(382, 33), 23)
	_objective = _label(mission, "探索霓虹海岸", Vector2(18, 76), Vector2(382, 48), 18, MUTED)
	_objective.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_mission_distance = _label(mission, "", Vector2(18, 127), Vector2(382, 23), 16, CORAL)
	var vitals: Panel = _panel(screen, Vector2(1264, 32), Vector2(304, 150))
	_stars = _label(vitals, "☆ ☆ ☆ ☆ ☆", Vector2(16, 8), Vector2(272, 32), 25, CORAL)
	_stars.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_cash = _label(vitals, "$ 250", Vector2(16, 43), Vector2(272, 48), 42, CREAM, true)
	_cash.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_label(vitals, "生命", Vector2(18, 100), Vector2(52, 24), 15, MUTED)
	_health = _bar(vitals, Vector2(76, 109), Vector2(210, 8), TEAL)
	_heat = _bar(vitals, Vector2(18, 135), Vector2(268, 3), CORAL)
	var radio: Panel = _panel(screen, Vector2(1264, 194), Vector2(304, 70), Color(0.04, 0.09, 0.12, 0.65))
	_label(radio, "ON AIR", Vector2(18, 8), Vector2(268, 23), 16, CORAL, true)
	_radio = _label(radio, "NEON FM", Vector2(18, 32), Vector2(268, 26), 19, CREAM)
	var map_card: Panel = _panel(screen, Vector2(32, 572), Vector2(294, 296))
	_district = _label(map_card, "霓虹海岸", Vector2(14, 7), Vector2(196, 28), 18)
	_clock_label = _label(map_card, "17:36", Vector2(212, 7), Vector2(68, 28), 18, TEAL, true)
	_clock_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_minimap = MapView.new()
	_minimap.name = "Minimap"
	_minimap.position = Vector2(14, 42)
	_minimap.size = Vector2(266, 205)
	map_card.add_child(_minimap)
	_label(map_card, "M / View   城市地图", Vector2(14, 256), Vector2(266, 25), 16, MUTED)
	_speed_card = _panel(screen, Vector2(1304, 697), Vector2(264, 171))
	_label(_speed_card, "COAST CRUISER", Vector2(18, 9), Vector2(226, 26), 17, TEAL, true)
	_speed = _label(_speed_card, "000", Vector2(17, 31), Vector2(162, 94), 82, CREAM, true)
	_label(_speed_card, "KM/H", Vector2(178, 80), Vector2(70, 32), 21, MUTED, true)
	_car_health = _bar(_speed_card, Vector2(18, 138), Vector2(228, 6), TEAL)
	_label(_speed_card, "VEHICLE CONDITION", Vector2(18, 148), Vector2(228, 20), 13, MUTED, true)
	_prompt_card = _panel(screen, Vector2(530, 800), Vector2(540, 60), Color(0.04, 0.09, 0.12, 0.94))
	_prompt = _label(_prompt_card, "", Vector2(12, 8), Vector2(516, 44), 23)
	_prompt.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER


func _bar(parent: Node, at: Vector2, dimensions: Vector2, color: Color) -> ProgressBar:
	var bar: ProgressBar = ProgressBar.new()
	bar.position = at
	bar.show_percentage = false
	bar.add_theme_font_size_override("font_size", 1)
	bar.value = 100
	bar.mouse_filter = Control.MOUSE_FILTER_IGNORE
	bar.add_theme_stylebox_override("background", _style(Color(CREAM, 0.12), Color.TRANSPARENT, 3))
	bar.add_theme_stylebox_override("fill", _style(color, Color.TRANSPARENT, 3))
	# Bars use exact heights; panel content margins are not appropriate here.
	for style_name: String in ["background", "fill"]:
		var style: StyleBoxFlat = bar.get_theme_stylebox(style_name).duplicate() as StyleBoxFlat
		style.set_content_margin_all(0)
		bar.add_theme_stylebox_override(style_name, style)
	parent.add_child(bar)
	# Apply the final size after the default percentage/font minimum has been removed.
	bar.size = dimensions
	return bar


func _build_pause() -> void:
	var screen: Control = _screen("paused")
	_label(screen, "TAKE A BREATH.", Vector2(100, 130), Vector2(700, 108), 90, CREAM, true)
	_label(screen, "城市会等你。", Vector2(104, 248), Vector2(560, 54), 30, MUTED)
	_label(screen, "PAUSED  /  旅程暂停", Vector2(104, 84), Vector2(500, 30), 20, TEAL, true)
	var panel: Panel = _panel(screen, Vector2(974, 96), Vector2(500, 712), Color(0.04, 0.09, 0.12, 0.95))
	_label(panel, "霓虹海岸", Vector2(32, 24), Vector2(430, 46), 32)
	_label(panel, "YOUR NEXT MOVE", Vector2(32, 75), Vector2(430, 28), 20, CORAL, true)
	_first_focus.paused = _button(panel, "继续旅程     /     RESUME", "resume", Vector2(32, 128), Vector2(436, 56), true)
	_button(panel, "城市地图", "map", Vector2(32, 198), Vector2(436, 52))
	_button(panel, "照片模式", "photo", Vector2(32, 264), Vector2(436, 52))
	_button(panel, "游戏设置", "settings", Vector2(32, 330), Vector2(436, 52))
	_button(panel, "操作指南", "controls", Vector2(32, 396), Vector2(436, 52))
	_button(panel, "重新开始", "restart", Vector2(32, 462), Vector2(436, 52))
	_button(panel, "返回主菜单", "title", Vector2(32, 528), Vector2(436, 52))
	_button(panel, "退出游戏", "quit", Vector2(32, 594), Vector2(436, 52))
	_label(screen, "Esc / Menu   继续游戏     ·     方向键 / 摇杆   选择     ·     Enter / A   确认", Vector2(104, 820), Vector2(1320, 34), 18, MUTED)


func _build_settings() -> void:
	var screen: Control = _screen("settings")
	var panel: Panel = _panel(screen, Vector2(276, 92), Vector2(1048, 716), Color(0.04, 0.09, 0.12, 0.97))
	_label(panel, "MAKE IT YOURS.", Vector2(38, 26), Vector2(900, 66), 54, CREAM, true)
	_label(panel, "游戏设置  /  调整属于你的海岸节奏", Vector2(40, 98), Vector2(940, 38), 22, MUTED)
	_label(panel, "主音量", Vector2(42, 172), Vector2(220, 44), 23)
	_volume = _slider(panel, "volume", Vector2(308, 179), 0.0, 1.0, 0.05)
	_volume_value = _label(panel, "50%", Vector2(858, 172), Vector2(132, 44), 23, TEAL, true)
	_label(panel, "视角灵敏度", Vector2(42, 250), Vector2(240, 44), 23)
	_sensitivity = _slider(panel, "sensitivity", Vector2(308, 257), 0.3, 2.0, 0.1)
	_sensitivity_value = _label(panel, "1.0×", Vector2(858, 250), Vector2(132, 44), 23, TEAL, true)
	_label(panel, "画面质量", Vector2(42, 328), Vector2(240, 44), 23)
	_quality = OptionButton.new()
	_quality.name = "QualitySelector"
	_quality.position = Vector2(308, 328)
	_quality.size = Vector2(500, 48)
	_quality.add_item("流畅   /   LOW")
	_quality.add_item("平衡   /   MEDIUM")
	_quality.add_item("精细   /   HIGH")
	_quality.select(2)
	_quality.add_theme_stylebox_override("normal", _style(Color(0.13, 0.21, 0.24), Color(CREAM, 0.2)))
	_quality.add_theme_stylebox_override("focus", _style(Color(TEAL, 0.18), TEAL))
	_quality.item_selected.connect(func(index: int) -> void: setting_changed.emit("quality", float(index)))
	panel.add_child(_quality)
	_invert = CheckButton.new()
	_invert.name = "InvertY"
	_invert.text = "反转垂直视角"
	_invert.position = Vector2(42, 408)
	_invert.size = Vector2(390, 48)
	_invert.toggled.connect(func(pressed: bool) -> void: setting_changed.emit("invert_y", 1.0 if pressed else 0.0))
	panel.add_child(_invert)
	_button(panel, "切换全屏", "toggle_fullscreen", Vector2(530, 408), Vector2(278, 48))
	_label(panel, "设置即时生效。键盘、鼠标与标准手柄均可操作。", Vector2(42, 488), Vector2(940, 36), 18, MUTED)
	_button(panel, "返回", "settings_back", Vector2(42, 576), Vector2(230, 56), true)
	_button(panel, "操作指南", "controls", Vector2(298, 576), Vector2(230, 56))
	_button(panel, "清除本机存档", "reset_confirm", Vector2(704, 576), Vector2(300, 56))
	_label(panel, "NEON COAST   /   PREFERENCES", Vector2(42, 660), Vector2(900, 28), 16, CORAL, true)
	_first_focus.settings = _volume


func _slider(parent: Node, key: String, at: Vector2, minimum: float, maximum: float, increment: float) -> HSlider:
	var slider: HSlider = HSlider.new()
	slider.name = key.capitalize() + "Slider"
	slider.set_meta("setting", key)
	slider.position = at
	slider.size = Vector2(500, 36)
	slider.min_value = minimum
	slider.max_value = maximum
	slider.step = increment
	slider.focus_mode = Control.FOCUS_ALL
	slider.value_changed.connect(func(value: float) -> void: setting_changed.emit(key, value))
	parent.add_child(slider)
	return slider


func _build_map() -> void:
	var screen: Control = _screen("map")
	_label(screen, "FIND YOUR WAY.", Vector2(76, 30), Vector2(900, 76), 62, CREAM, true)
	_label(screen, "城市地图", Vector2(1190, 48), Vector2(320, 44), 27, TEAL).horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_fullmap = MapView.new()
	_fullmap.name = "FullMap"
	_fullmap.full_map = true
	_fullmap.position = Vector2(76, 122)
	_fullmap.size = Vector2(1000, 690)
	_fullmap.waypoint_selected.connect(func(at: Vector3) -> void: waypoint_selected.emit(at))
	screen.add_child(_fullmap)
	var side: Panel = _panel(screen, Vector2(1104, 122), Vector2(420, 690), Color(0.04, 0.09, 0.12, 0.95))
	_label(side, "YOUR DESTINATION", Vector2(26, 24), Vector2(368, 34), 26, CORAL, true)
	_map_objective = _label(side, "自由探索", Vector2(26, 82), Vector2(368, 126), 24)
	_map_objective.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_map_distance = _label(side, "", Vector2(26, 220), Vector2(368, 42), 31, TEAL, true)
	_label(side, "●  金色标记   任务目标\n◆  青色标记   自定路线\n▲  白色箭头   当前位置", Vector2(26, 288), Vector2(368, 124), 20, MUTED)
	_map_stats = _label(side, "城市探索", Vector2(26, 434), Vector2(368, 54), 18, MUTED)
	_button(side, "清除自定路线", "clear_waypoint", Vector2(26, 510), Vector2(368, 54))
	_button(side, "返回街头", "map_close", Vector2(26, 586), Vector2(368, 58), true)
	_label(screen, "点击设置路线  ·  方向键 / 摇杆 移动光标  ·  Enter / A 确认  ·  Tab / RB·LB 切换控件  ·  M / B 返回", Vector2(76, 837), Vector2(1440, 32), 18, MUTED)
	_first_focus.map = _fullmap


func _build_photo() -> void:
	var screen: Control = _screen("photo")
	_label(screen, "NEON COAST  /  PHOTO MODE", Vector2(36, 24), Vector2(800, 36), 26, CREAM, true)
	var strip: Panel = _panel(screen, Vector2(230, 812), Vector2(1140, 54), Color(0.04, 0.09, 0.12, 0.78))
	_label(strip, "WASD / 左摇杆 移动   ·   鼠标 / 右摇杆 取景   ·   空格·Ctrl / A·B 升降   ·   F12 / X 拍照   ·   Esc / Menu 返回", Vector2(16, 6), Vector2(1108, 42), 17).horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER


func _build_help() -> void:
	_help = Control.new()
	_help.name = "ControlsHelp"
	_help.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_help.mouse_filter = Control.MOUSE_FILTER_STOP
	_root.add_child(_help)
	var blocker: ColorRect = ColorRect.new()
	blocker.color = Color(0.02, 0.05, 0.07, 0.94)
	blocker.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	blocker.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_help.add_child(blocker)
	_label(_help, "OWN THE STREETS.", Vector2(128, 66), Vector2(1300, 90), 76, CREAM, true)
	_label(_help, "操作指南  /  KEYBOARD + CONTROLLER", Vector2(132, 167), Vector2(1280, 42), 25, TEAL)
	var left: Panel = _panel(_help, Vector2(128, 238), Vector2(650, 472))
	var right: Panel = _panel(_help, Vector2(810, 238), Vector2(662, 472))
	_label(left, "ON FOOT  /  步行", Vector2(26, 16), Vector2(590, 40), 27, CORAL, true)
	_label(left, "移动 / 视角    WASD + 鼠标 / 双摇杆\n奔跑    Shift / 按下左摇杆\n跳跃    空格 / A\n交互·上下车    F / Y\n瞄准 / 射击    鼠标右键·左键 / LT·RT\n地图    M / View·Back\n暂停    Esc / Menu·Start", Vector2(26, 77), Vector2(600, 350), 21)
	_label(right, "BEHIND THE WHEEL  /  驾驶", Vector2(26, 16), Vector2(610, 40), 27, CORAL, true)
	_label(right, "油门 / 倒车    W·S / RT·LT\n转向    A·D / 左摇杆\n手刹    空格 / B    ·    加速    Shift / A\n视角    V / 按下右摇杆\n电台    R / 十字键 →    ·    喇叭    H / LB\n车灯    L / 十字键 ↑\n道路救援    T / 十字键 ↓", Vector2(26, 77), Vector2(610, 350), 21)
	_label(_help, "F6 或暂停菜单进入照片模式。F12 / X 保存照片。手柄按键采用 Xbox 标记。", Vector2(132, 733), Vector2(1320, 40), 18, MUTED)
	_button(_help, "返回旅程", "help_back", Vector2(1164, 802), Vector2(308, 56), true)
	_help.hide()


func _build_notifications() -> void:
	_toast = _panel(_root, Vector2(508, 35), Vector2(584, 112), Color(0.04, 0.09, 0.12, 0.96))
	_toast_title = _label(_toast, "", Vector2(22, 12), Vector2(540, 37), 25, CREAM)
	_toast_subtitle = _label(_toast, "", Vector2(22, 54), Vector2(540, 46), 17, MUTED)
	_toast_subtitle.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_toast.hide()
	_completion = _panel(_root, Vector2(470, 188), Vector2(660, 136), Color(0.05, 0.15, 0.17, 0.97))
	_label(_completion, "STORY COMPLETE", Vector2(24, 9), Vector2(610, 31), 25, TEAL, true)
	_completion_title = _label(_completion, "", Vector2(24, 44), Vector2(610, 43), 29)
	_completion_reward = _label(_completion, "", Vector2(24, 91), Vector2(610, 30), 24, CORAL, true)
	_completion.hide()


func set_mode(mode: String) -> void:
	current_mode = mode
	if _root == null:
		return
	_help_visible = false
	_help.hide()
	for key: String in _screens:
		_screens[key].visible = key == mode
	if mode == "map":
		var position: Vector3 = current_state.get("player_position", Vector3.ZERO)
		_fullmap.cursor_world = Vector2(position.x, position.z).clamp(Vector2(-246, -246), Vector2(246, 246))
	_toast.visible = mode == "playing" and _toast_time > 0.0
	_completion.visible = mode == "playing" and _completion_time > 0.0
	var focus: Control = _root.get_viewport().gui_get_focus_owner()
	if focus != null:
		focus.release_focus()
	if _first_focus.has(mode):
		_focus_current.call_deferred()
	_backdrop.queue_redraw()


func _focus_current() -> void:
	if not _help_visible and _first_focus.has(current_mode):
		var control: Control = _first_focus[current_mode]
		if control.is_visible_in_tree():
			control.grab_focus()


func update_state(data: Dictionary) -> void:
	current_state = data
	if _root == null:
		return
	_continue.disabled = not bool(data.get("save_exists", false))
	_mission_title.text = str(data.get("mission_title", "自由探索"))
	_objective.text = str(data.get("objective", "沿着海岸寻找新的故事。"))
	_mission_distance.text = "目的地  %s" % _distance(float(data.get("distance", 0.0))) if bool(data.get("has_objective", false)) else "FREE ROAM  /  自由探索"
	_cash.text = "$ %s" % str(int(data.get("cash", 0)))
	var wanted: int = clampi(int(data.get("wanted", 0)), 0, 5)
	_stars.text = "★ ".repeat(wanted) + "☆ ".repeat(5 - wanted)
	_health.value = clampf(float(data.get("health", 100.0)), 0, 100)
	_heat.value = clampf(float(data.get("heat", 0.0)) / 1.2, 0, 100)
	_district.text = str(data.get("district", "霓虹海岸"))
	_clock_label.text = str(data.get("time", "17:36"))
	_radio.text = str(data.get("radio", "NEON FM"))
	_speed_card.visible = bool(data.get("vehicle", false))
	_speed.text = "%03d" % int(float(data.get("speed", 0.0)))
	_car_health.value = clampf(float(data.get("car_health", 100.0)), 0, 100)
	_prompt.text = str(data.get("prompt", ""))
	_prompt_card.visible = not _prompt.text.is_empty()
	_minimap.set_data(data, map_blocks)
	_fullmap.set_data(data, map_blocks)
	_map_objective.text = str(data.get("objective", "自由探索这座城市。"))
	_map_distance.text = _distance(float(data.get("distance", 0.0))) if bool(data.get("has_objective", false)) else "FREE ROAM"
	_map_stats.text = "海岸记忆  %d / %d\n当前区域  %s" % [int(data.get("collectibles", 0)), int(data.get("collectibles_total", 10)), str(data.get("district", "霓虹海岸"))]
	var preferences: Dictionary = data.get("settings", {})
	if not preferences.is_empty():
		_volume.set_value_no_signal(float(preferences.get("volume", 0.5)))
		_sensitivity.set_value_no_signal(float(preferences.get("sensitivity", 1.0)))
		_volume_value.text = "%d%%" % int(_volume.value * 100)
		_sensitivity_value.text = "%.1f×" % _sensitivity.value
		var quality: String = str(preferences.get("quality", "high"))
		_quality.select(maxi(0, ["low", "medium", "high"].find(quality)))
		_invert.set_pressed_no_signal(bool(preferences.get("invert_y", false)))
	_backdrop.queue_redraw()


func _distance(metres: float) -> String:
	return "%.2f km" % (metres / 1000.0) if metres >= 1000.0 else "%d m" % int(metres)


func set_map_blocks(blocks: Array) -> void:
	map_blocks = blocks
	if _minimap != null:
		_minimap.set_data(current_state, blocks)
		_fullmap.set_data(current_state, blocks)


func notify(text: String, subtitle: String = "") -> void:
	if _toast == null:
		return
	_toast_title.text = text
	_toast_subtitle.text = subtitle
	_toast_time = 5.0
	_toast.visible = current_mode == "playing"


func show_mission_complete(title: String, reward: int) -> void:
	if _completion == null:
		return
	_completion_title.text = title
	_completion_reward.text = "+ $%d  /  奖励已到账" % reward
	_completion_time = 6.0
	_completion.visible = current_mode == "playing"


func _show_help() -> void:
	_help_return_focus = _root.get_viewport().gui_get_focus_owner()
	_help_visible = true
	_help.show()
	for child: Node in _help.get_children():
		if child is Button:
			# The help overlay is modal even though the previous screen remains visible.
			child.focus_neighbor_top = child.get_path()
			child.focus_neighbor_bottom = child.get_path()
			child.focus_neighbor_left = child.get_path()
			child.focus_neighbor_right = child.get_path()
			child.focus_next = child.get_path()
			child.focus_previous = child.get_path()
			child.grab_focus()


func _hide_help() -> void:
	_help_visible = false
	_help.hide()
	if is_instance_valid(_help_return_focus) and _help_return_focus.is_visible_in_tree():
		_help_return_focus.grab_focus()
	else:
		_focus_current()


func _input(event: InputEvent) -> void:
	if _help_visible and (event.is_action_pressed("ui_cancel") or event.is_action_pressed("pause")):
		_hide_help()
		get_viewport().set_input_as_handled()


func _process(delta: float) -> void:
	if _toast_time > 0.0:
		_toast_time = maxf(0.0, _toast_time - delta)
		_toast.visible = current_mode == "playing" and _toast_time > 0.0 and not _help_visible
	if _completion_time > 0.0:
		_completion_time = maxf(0.0, _completion_time - delta)
		_completion.visible = current_mode == "playing" and _completion_time > 0.0 and not _help_visible


func _paint_backdrop(canvas: Control) -> void:
	var dimensions: Vector2 = canvas.size
	if current_mode == "title":
		canvas.draw_polygon(PackedVector2Array([Vector2.ZERO, Vector2(dimensions.x,0), dimensions, Vector2(0,dimensions.y)]), PackedColorArray([Color(INK,0.96), Color(INK,0.08), Color(INK,0.08), Color(INK,0.96)]))
		canvas.draw_line(Vector2(84, 91), Vector2(456, 91), Color(TEAL, 0.65), 1.0)
		canvas.draw_line(Vector2(84, 826), Vector2(1514, 826), Color(CREAM, 0.18), 1.0)
		var sun: Vector2 = Vector2(1044, 269)
		canvas.draw_arc(sun, 126, -PI, 0, 64, Color(CORAL, 0.35), 2.0, true)
		for stripe: int in range(7):
			var yy: float = float(stripe) * 15.0
			var half: float = sqrt(maxf(0.0, 126.0 * 126.0 - yy * yy))
			canvas.draw_line(sun + Vector2(-half, yy), sun + Vector2(half, yy), Color(CORAL, 0.24), 2.0, true)
	elif current_mode in ["paused", "settings", "map"]:
		canvas.draw_rect(Rect2(Vector2.ZERO, dimensions), Color(0.015, 0.035, 0.05, 0.79 if current_mode == "map" else 0.69))
	if current_mode == "playing" and bool(current_state.get("aiming", false)):
		var center: Vector2 = dimensions * 0.5
		for direction: Vector2 in [Vector2.LEFT, Vector2.RIGHT, Vector2.UP, Vector2.DOWN]:
			canvas.draw_line(center + direction * 5, center + direction * 12, Color(CREAM, 0.9), 2.0)

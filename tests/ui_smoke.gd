extends SceneTree
## Exercise the real native HUD with synthetic keyboard, gamepad and mouse input.
## Godot --headless --path . --script res://tests/ui_smoke.gd -- --test-mode
## Requires the completed HUD. Never writes profile/settings or changes HUD code.

const GAME = preload("res://scripts/game.gd")
var game: Node3D
var failures: Array[String] = []
var actions_seen: Array[String] = []
var settings_seen: Array[String] = []
var waypoints_seen: Array[Vector3] = []
var initial_user_files: Dictionary = {}
var checks_passed: int = 0


func _initialize() -> void:
	if not "--test-mode" in OS.get_cmdline_user_args():
		push_error("Refusing UI smoke without --test-mode; real profile/settings must remain untouched.")
		quit(2)
		return
	_run.call_deferred()


func _run() -> void:
	initial_user_files = _user_files_snapshot()
	root.size = Vector2i(1600, 900)
	game = GAME.new()
	game._test_mode = true
	root.add_child(game)
	game._test_mode = true
	game.hud.action_requested.connect(_action_received)
	game.hud.setting_changed.connect(_setting_received)
	game.hud.waypoint_selected.connect(_waypoint_received)
	await _frames(12)
	_check(game.hud.get_child_count() > 0, "real HUD contains controls")
	if game.hud.get_child_count() == 0:
		await _finish()
		return
	_dump_visible_controls("title")
	var start_button: BaseButton = _find_button("start", ["开始", "启程", "出发", "新游戏", "新旅程", "START", "NEW GAME"])
	_check(start_button != null, "title exposes a start button")
	if start_button != null:
		start_button.grab_focus()
		await _frames(2)
		_check(root.gui_get_focus_owner() == start_button, "start button accepts keyboard focus")
		await _tap_key(KEY_ENTER)
		_check(game.mode == "playing" and actions_seen.has("start"), "keyboard Enter activates the focused start button")
	if game.mode != "playing":
		game._start_game(false)
		await _frames(4)

	await _tap_pad(JOY_BUTTON_START)
	_check(game.mode == "paused" and paused, "controller Menu opens pause")
	_dump_visible_controls("paused")
	var focused_before: Control = root.gui_get_focus_owner()
	_check(focused_before != null and focused_before.is_visible_in_tree(), "pause grants focus to a visible menu control")
	await _tap_pad(JOY_BUTTON_DPAD_DOWN)
	var focused_after: Control = root.gui_get_focus_owner()
	_check(focused_after != null and focused_after != focused_before and focused_after.is_visible_in_tree(), "controller D-pad moves native menu focus")
	var help_button: BaseButton = _find_button("controls", ["操作指南"])
	_check(help_button != null, "pause exposes the controls guide")
	if help_button != null:
		help_button.grab_focus()
		await _tap_key(KEY_ENTER)
		_check(game.hud._help_visible and game.mode == "paused", "keyboard opens controls help without resuming simulation")
		var help_focus: Control = root.gui_get_focus_owner()
		await _tap_pad(JOY_BUTTON_DPAD_UP)
		_check(root.gui_get_focus_owner() == help_focus, "help modal keeps controller focus out of background menus")
		await _tap_key(KEY_ESCAPE)
		_check(not game.hud._help_visible and game.mode == "paused", "Escape closes help before the game pause handler")
	var settings_button: BaseButton = _find_button("settings", ["设置", "SETTINGS", "OPTIONS"])
	_check(settings_button != null, "pause exposes a settings button")
	if settings_button != null:
		settings_button.grab_focus()
		await _tap_pad(JOY_BUTTON_A)
		_check(game.mode == "settings" and actions_seen.has("settings"), "controller A activates the focused settings button")
	if game.mode != "settings":
		game._ui_action("settings")
	await _frames(8)
	_dump_visible_controls("settings")
	await _check_settings_slider()
	var back_button: BaseButton = _find_button("settings_back", ["返回", "BACK", "完成", "DONE"])
	if back_button == null:
		back_button = _find_button("back", ["返回", "BACK", "完成", "DONE"])
	_check(back_button != null, "settings exposes a back button")
	if back_button != null:
		await _click_control(back_button)
		_check(game.mode == "paused", "mouse click on settings back returns to pause")
	if game.mode != "paused":
		game._set_mode("paused")
	await _frames(4)
	var resume_button: BaseButton = _find_button("resume", ["继续", "返回游戏", "恢复", "RESUME"])
	_check(resume_button != null, "pause exposes a resume button")
	if resume_button != null:
		resume_button.grab_focus()
		await _tap_pad(JOY_BUTTON_A)
		_check(game.mode == "playing" and not paused, "controller A resumes through the real native button")
	if game.mode != "playing":
		game._set_mode("playing")
	await _tap_key(KEY_M)
	_check(game.mode == "map" and paused, "keyboard M opens the full map")
	_dump_visible_controls("map")
	await _check_map_click()
	var full_map: Control = _find_map_control()
	var clear_route: BaseButton = _find_button("clear_waypoint", ["清除自定路线"])
	var close_map: BaseButton = _find_button("map_close", ["返回街头"])
	await _tap_pad(JOY_BUTTON_RIGHT_SHOULDER)
	_check(clear_route != null and root.gui_get_focus_owner() == clear_route, "controller RB leaves map cursor and focuses clear-waypoint button")
	await _tap_pad(JOY_BUTTON_LEFT_SHOULDER)
	_check(root.gui_get_focus_owner() == full_map, "controller LB returns focus from map controls to the cursor")
	await _tap_pad(JOY_BUTTON_RIGHT_SHOULDER)
	await _tap_pad(JOY_BUTTON_A)
	_check(not game.waypoint_active and actions_seen.has("clear_waypoint"), "controller A on the real clear-waypoint button removes the selected route")
	await _tap_pad(JOY_BUTTON_RIGHT_SHOULDER)
	_check(close_map != null and root.gui_get_focus_owner() == close_map, "controller RB reaches the map back button")
	await _tap_pad(JOY_BUTTON_A)
	_check(game.mode == "playing" and actions_seen.has("map_close"), "controller A on the map back button resumes gameplay")
	await _tap_key(KEY_M)
	await _tap_pad(JOY_BUTTON_B)
	_check(game.mode == "playing" and not paused, "controller B closes the map")
	if game.mode != "playing":
		game._set_mode("playing")
	await _tap_key(KEY_ESCAPE)
	_check(game.mode == "paused", "keyboard Escape opens pause")
	await _tap_key(KEY_ESCAPE)
	_check(game.mode == "playing", "keyboard Escape resumes the game")
	await _tap_key(KEY_ESCAPE)
	var photo_button: BaseButton = _find_button("photo", ["照片模式"])
	_check(photo_button != null, "pause exposes photo mode")
	if photo_button != null:
		photo_button.grab_focus()
		await _tap_pad(JOY_BUTTON_A)
		_check(game.mode == "photo" and root.gui_get_focus_owner() == null, "photo mode has no focused button to intercept camera controls")
		await _tap_pad(JOY_BUTTON_B)
		_check(game.mode == "photo", "photo controller B remains a camera descent control")
		await _tap_key(KEY_ESCAPE)
		_check(game.mode == "playing", "Escape exits photo mode")
	_check(_user_files_snapshot() == initial_user_files, "profile/settings files remain byte-for-byte unchanged")
	await _finish()


func _check_settings_slider() -> void:
	var slider: Slider = _find_slider("volume")
	_check(slider != null, "settings contains a real volume slider")
	if slider == null:
		return
	var old_volume: float = float(game.settings.volume)
	var direction: Key = KEY_RIGHT if old_volume < 0.9 else KEY_LEFT
	slider.grab_focus()
	await _frames(2)
	_check(root.gui_get_focus_owner() == slider, "volume slider accepts keyboard focus")
	await _tap_key(direction)
	await _tap_key(direction)
	_check(not is_equal_approx(float(game.settings.volume), old_volume) and settings_seen.has("volume"), "keyboard slider adjustment emits volume and updates game settings")
	var after_keyboard: float = float(game.settings.volume)
	await _tap_pad(JOY_BUTTON_DPAD_RIGHT if after_keyboard < 0.9 else JOY_BUTTON_DPAD_LEFT)
	_check(not is_equal_approx(float(game.settings.volume), after_keyboard), "controller D-pad adjusts the focused volume slider")


func _check_map_click() -> void:
	# Finalize the selector against Claude's actual control tree before running.
	var map_control: Control = _find_map_control()
	_check(map_control != null, "full map exposes a visible interactive map control")
	if map_control == null:
		return
	var previous_count: int = waypoints_seen.size()
	await _click_control(map_control, Vector2(0.58, 0.46))
	_check(waypoints_seen.size() > previous_count and game.waypoint_active, "clicking the actual map emits a waypoint and updates the game")
	if waypoints_seen.size() > previous_count:
		var point: Vector3 = waypoints_seen.back()
		_check(point.is_finite() and absf(point.x) < 350.0 and absf(point.z) < 350.0, "map click converts to finite city world coordinates")


func _find_button(action: String, labels: Array) -> BaseButton:
	var fallback: BaseButton = null
	for node: Node in _all_nodes(game.hud):
		if not node is BaseButton:
			continue
		var button: BaseButton = node as BaseButton
		if not button.is_visible_in_tree() or button.disabled:
			continue
		if _has_bound_argument(button, "pressed", action) or str(button.get_meta("action", "")) == action:
			return button
		var label: String = _control_text(button).to_upper()
		for token: String in labels:
			if label.contains(token.to_upper()):
				fallback = button
	return fallback


func _find_slider(key: String) -> Slider:
	var fallback: Slider = null
	for node: Node in _all_nodes(game.hud):
		if not node is Slider:
			continue
		var slider: Slider = node as Slider
		if not slider.is_visible_in_tree():
			continue
		if _has_bound_argument(slider, "value_changed", key) or str(slider.get_meta("setting", "")) == key:
			return slider
		var identifying_text: String = str(slider.name) + " " + _control_text(slider.get_parent())
		if identifying_text.to_lower().contains(key) or (key == "volume" and identifying_text.contains("音量")):
			return slider
		if fallback == null:
			fallback = slider
	return fallback


func _find_map_control() -> Control:
	var selected: Control = null
	var selected_area: float = 0.0
	for node: Node in _all_nodes(game.hud):
		if not node is Control:
			continue
		var control: Control = node as Control
		if not control.is_visible_in_tree() or control.mouse_filter == Control.MOUSE_FILTER_IGNORE:
			continue
		if not str(control.name).to_lower().contains("map"):
			continue
		var area: float = control.size.x * control.size.y
		if control.size.x >= 300.0 and control.size.y >= 250.0 and area > selected_area:
			selected = control
			selected_area = area
	return selected


func _has_bound_argument(node: Object, signal_name: String, expected: String) -> bool:
	for connection: Dictionary in node.get_signal_connection_list(signal_name):
		var target: Callable = connection["callable"]
		for argument: Variant in target.get_bound_arguments():
			if typeof(argument) == TYPE_STRING and str(argument) == expected:
				return true
	return false


func _control_text(node: Node) -> String:
	var text: String = ""
	if node is Button:
		text += node.text
	elif node is Label:
		text += node.text
	elif node is RichTextLabel:
		text += node.text
	for child: Node in node.get_children():
		text += " " + _control_text(child)
	return text.strip_edges()


func _all_nodes(parent: Node) -> Array[Node]:
	var result: Array[Node] = []
	for child: Node in parent.get_children():
		result.append(child)
		result.append_array(_all_nodes(child))
	return result


func _dump_visible_controls(label: String) -> void:
	print("UI_TREE ", label)
	for node: Node in _all_nodes(game.hud):
		if node is Control and node.is_visible_in_tree() and (node is BaseButton or node is Slider or str(node.name).to_lower().contains("map")):
			print("  ", node.get_path(), " class=", node.get_class(), " text=", _control_text(node).left(90), " rect=", node.get_global_rect(), " focus=", node.focus_mode, " mouse=", node.mouse_filter)


func _tap_key(code: Key) -> void:
	_key(code, true)
	await _frames(2)
	_key(code, false)
	await _frames(3)


func _key(code: Key, pressed: bool) -> void:
	var event: InputEventKey = InputEventKey.new()
	event.physical_keycode = code
	event.keycode = code
	event.pressed = pressed
	Input.parse_input_event(event)


func _tap_pad(button: JoyButton) -> void:
	var event: InputEventJoypadButton = InputEventJoypadButton.new()
	event.device = 15
	event.button_index = button
	event.pressed = true
	Input.parse_input_event(event)
	await _frames(2)
	event = InputEventJoypadButton.new()
	event.device = 15
	event.button_index = button
	event.pressed = false
	Input.parse_input_event(event)
	await _frames(3)


func _click_control(control: Control, fraction: Vector2 = Vector2(0.5, 0.5)) -> void:
	var point: Vector2 = control.get_global_transform_with_canvas() * (control.size * fraction)
	var motion: InputEventMouseMotion = InputEventMouseMotion.new()
	motion.position = point
	motion.global_position = point
	root.push_input(motion, true)
	await _frames(2)
	for pressed: bool in [true, false]:
		var event: InputEventMouseButton = InputEventMouseButton.new()
		event.button_index = MOUSE_BUTTON_LEFT
		event.button_mask = MOUSE_BUTTON_MASK_LEFT if pressed else 0
		event.pressed = pressed
		event.position = point
		event.global_position = point
		root.push_input(event, true)
		await _frames(2)
	await _frames(4)


func _frames(count: int) -> void:
	for frame: int in count:
		await process_frame


func _user_files_snapshot() -> Dictionary:
	var snapshot: Dictionary = {}
	for filename: String in ["profile.json", "settings.json"]:
		for suffix: String in ["", ".bak", ".tmp"]:
			var path: String = "user://" + filename + suffix
			var exists: bool = FileAccess.file_exists(path)
			snapshot[path] = {"exists": exists, "sha256": FileAccess.get_sha256(path) if exists else "", "modified": FileAccess.get_modified_time(path) if exists else 0}
	return snapshot


func _action_received(action: String) -> void:
	actions_seen.append(action)
	print("UI_ACTION ", action)


func _setting_received(key: String, value: float) -> void:
	settings_seen.append(key)
	print("UI_SETTING ", key, "=", value)


func _waypoint_received(position: Vector3) -> void:
	waypoints_seen.append(position)
	print("UI_WAYPOINT ", position)


func _check(condition: bool, message: String) -> void:
	if condition:
		checks_passed += 1
		print("PASS ", message)
	else:
		failures.append(message)
		push_error("FAIL " + message)


func _finish() -> void:
	paused = false
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	_check(_user_files_snapshot() == initial_user_files, "final profile/settings snapshot is unchanged")
	game.queue_free()
	await process_frame
	# Fixed-FPS tests advance timers faster than the actual audio mixer thread.
	OS.delay_msec(100)
	await process_frame
	print("UI_SMOKE_COMPLETE passed=", checks_passed, " failures=", failures.size(), " | physical_controller_verified=false")
	quit(0 if failures.is_empty() else 1)

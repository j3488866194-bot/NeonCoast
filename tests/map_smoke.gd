extends SceneTree
## Focused tests against Claude's actual map Control, using native viewport input.
## Godot --headless --path . --fixed-fps 60 --script res://tests/map_smoke.gd -- --test-mode

const MAP = preload("res://scripts/ui_map.gd")
const INPUT_SETUP = preload("res://scripts/input_setup.gd")

var map: Control
var failures: Array[String] = []
var selected: Array[Vector3] = []
var passed: int = 0


func _initialize() -> void:
	if not "--test-mode" in OS.get_cmdline_user_args():
		push_error("Map smoke requires --test-mode.")
		quit(2)
		return
	_run.call_deferred()


func _run() -> void:
	INPUT_SETUP.configure()
	root.size = Vector2i(1600, 900)
	map = MAP.new()
	map.name = "ActualMap"
	map.full_map = true
	map.position = Vector2(80, 80)
	map.size = Vector2(1000, 720)
	root.add_child(map)
	map.waypoint_selected.connect(_selected)
	map.set_data({"player_position": Vector3(72, 0, 144), "player_yaw": 0.4, "has_objective": true, "objective_position": Vector3(-144, 0, -72), "wanted": 2}, [{"x": 36, "z": 36, "w": 30, "d": 40, "color": Color.CORAL}])
	await _frames(5)
	_check(map.full_map and map.mouse_filter == Control.MOUSE_FILTER_STOP and map.focus_mode == Control.FOCUS_ALL, "full map accepts pointer input and keyboard/gamepad focus")
	_check(map.process_mode == Node.PROCESS_MODE_ALWAYS and map.clip_contents, "map keeps processing while paused and clips its drawing")
	_check(map.cursor_world.is_equal_approx(Vector2(72, 144)), "initial cursor uses player X/Z coordinates")
	map._update_view()
	var origin: Vector2 = map._to_map(Vector2.ZERO)
	var north: Vector2 = map._to_map(Vector2(0, -72))
	var east: Vector2 = map._to_map(Vector2(72, 0))
	_check(north.y < origin.y and is_equal_approx(north.x, origin.x), "negative world Z points north/up on the map")
	_check(east.x > origin.x and is_equal_approx(east.y, origin.y), "positive world X points east/right on the map")
	_check(is_equal_approx(origin.distance_to(north), origin.distance_to(east)), "map preserves equal X/Z world distances")
	for point: Vector2 in [Vector2(-246, -246), Vector2.ZERO, Vector2(216, 144), Vector2(310, 250)]:
		var recovered: Vector2 = map._to_world(map._to_map(point))
		_check(recovered.distance_to(point) < 0.001, "world/map transform round trip at " + str(point))

	var click_world: Vector2 = Vector2(144, -72)
	await _click_local(map._to_map(click_world))
	_check(selected.size() == 1 and selected.back().distance_to(Vector3(144, 0, -72)) < 0.001, "real viewport mouse click converts to the expected world X/Z waypoint")
	_check(map.has_focus(), "clicking the full map grants native focus")
	var cursor_before: Vector2 = map.cursor_world
	_key(KEY_RIGHT, true)
	await _frames(18)
	_key(KEY_RIGHT, false)
	await _frames(2)
	_check(map.cursor_world.x > cursor_before.x + 0.2 and is_equal_approx(map.cursor_world.y, cursor_before.y), "held keyboard Right moves the focused cursor east")
	cursor_before = map.cursor_world
	_pad(JOY_BUTTON_DPAD_UP, true)
	await _frames(18)
	_pad(JOY_BUTTON_DPAD_UP, false)
	await _frames(2)
	_check(map.cursor_world.y < cursor_before.y - 0.2 and is_equal_approx(map.cursor_world.x, cursor_before.x), "controller D-pad Up moves the focused cursor north")
	cursor_before = map.cursor_world
	_axis(JOY_AXIS_LEFT_X, -0.8)
	await _frames(18)
	_axis(JOY_AXIS_LEFT_X, 0.0)
	await _frames(2)
	_check(map.cursor_world.x < cursor_before.x - 0.2, "controller left stick moves the focused map cursor")
	var selected_before: int = selected.size()
	_key(KEY_ENTER, true)
	await _frames(2)
	_key(KEY_ENTER, false)
	await _frames(2)
	_check(selected.size() == selected_before + 1 and selected.back().distance_to(Vector3(map.cursor_world.x, 0, map.cursor_world.y)) < 0.001, "keyboard Enter selects the current map cursor")
	selected_before = selected.size()
	_pad(JOY_BUTTON_A, true)
	await _frames(2)
	_pad(JOY_BUTTON_A, false)
	await _frames(2)
	_check(selected.size() == selected_before + 1, "controller A selects the current map cursor")

	map.cursor_world = Vector2(245.9, 245.9)
	_key(KEY_RIGHT, true)
	_key(KEY_DOWN, true)
	await _frames(18)
	_key(KEY_RIGHT, false)
	_key(KEY_DOWN, false)
	await _frames(2)
	_check(map.cursor_world.is_equal_approx(Vector2(246, 246)), "diagonal cursor motion clamps to safe city bounds")
	await _click_local(Vector2(3, 3))
	_check(selected.back().is_equal_approx(Vector3(-246, 0, -246)), "click in outer map margin clamps waypoint to safe world bounds")
	selected_before = selected.size()
	await _click_viewport(Vector2(1500, 850))
	_check(selected.size() == selected_before, "mouse clicks outside the map do not create waypoints")

	map.full_map = false
	map.size = Vector2(250, 205)
	map.set_data({"player_position": Vector3(-144, 0, 72)}, [])
	map._update_view()
	await _frames(3)
	_check(not map.full_map and map.mouse_filter == Control.MOUSE_FILTER_IGNORE and map.focus_mode == Control.FOCUS_NONE and not map.has_focus(), "minimap ignores pointer events and releases focus")
	_check(map._to_map(Vector2(-144, 72)).distance_to(map.size * 0.5) < 0.001, "minimap centers the current player position")
	selected_before = selected.size()
	await _click_local(map.size * 0.5)
	_pad(JOY_BUTTON_A, true)
	await _frames(2)
	_pad(JOY_BUTTON_A, false)
	await _frames(2)
	_check(selected.size() == selected_before, "minimap cannot select waypoints by mouse or controller")
	map.full_map = true
	map.size = Vector2(920, 720)
	map.grab_focus()
	await _frames(3)
	_check(map.has_focus() and map.mouse_filter == Control.MOUSE_FILTER_STOP, "switching back restores interactive full-map focus")
	paused = true
	map.cursor_world = Vector2.ZERO
	cursor_before = map.cursor_world
	_key(KEY_LEFT, true)
	await _frames(18)
	_key(KEY_LEFT, false)
	await _frames(2)
	_check(map.cursor_world.x < cursor_before.x - 0.2, "map cursor remains responsive while the SceneTree is paused")
	paused = false
	map.queue_free()
	await _frames(2)
	print("MAP_SMOKE_COMPLETE passed=", passed, " failures=", failures.size(), " | physical_controller_verified=false")
	quit(0 if failures.is_empty() else 1)


func _click_local(point: Vector2) -> void:
	await _click_viewport(map.get_global_transform_with_canvas() * point)


func _click_viewport(point: Vector2) -> void:
	var motion: InputEventMouseMotion = InputEventMouseMotion.new()
	motion.position = point
	motion.global_position = point
	root.push_input(motion, true)
	await _frames(2)
	for is_pressed: bool in [true, false]:
		var event: InputEventMouseButton = InputEventMouseButton.new()
		event.button_index = MOUSE_BUTTON_LEFT
		event.button_mask = MOUSE_BUTTON_MASK_LEFT if is_pressed else 0
		event.position = point
		event.global_position = point
		event.pressed = is_pressed
		root.push_input(event, true)
		await _frames(2)


func _key(code: Key, is_pressed: bool) -> void:
	var event: InputEventKey = InputEventKey.new()
	event.keycode = code
	event.physical_keycode = code
	event.pressed = is_pressed
	Input.parse_input_event(event)


func _pad(button: JoyButton, is_pressed: bool) -> void:
	var event: InputEventJoypadButton = InputEventJoypadButton.new()
	event.device = 15
	event.button_index = button
	event.pressed = is_pressed
	Input.parse_input_event(event)


func _axis(axis: JoyAxis, value: float) -> void:
	var event: InputEventJoypadMotion = InputEventJoypadMotion.new()
	event.device = 15
	event.axis = axis
	event.axis_value = value
	Input.parse_input_event(event)


func _frames(count: int) -> void:
	for frame: int in count:
		await process_frame


func _selected(position: Vector3) -> void:
	selected.append(position)
	print("MAP_WAYPOINT ", position)


func _check(condition: bool, message: String) -> void:
	if condition:
		passed += 1
		print("PASS ", message)
	else:
		failures.append(message)
		push_error("FAIL " + message)

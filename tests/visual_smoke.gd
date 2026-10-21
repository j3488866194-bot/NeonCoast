extends SceneTree
## Render the actual complete game. Run only after the HUD frontend is ready.
## Godot --path . --script res://tests/visual_smoke.gd -- --test-mode
## No --headless: this test requires a real rendering device and visible window.
## Writes screenshots/report under artifacts; never calls a settings/profile write.

const GAME = preload("res://scripts/game.gd")
const ARTIFACT_DIR: String = "res://artifacts"
const BASE_SIZE: Vector2i = Vector2i(1600, 900)

var game: Node3D
var failures: Array[String] = []
var captures: Array[Dictionary] = []
var performance_snapshot: Dictionary = {}
var initial_user_files: Dictionary = {}
var started_ms: int = 0


func _initialize() -> void:
	if not "--test-mode" in OS.get_cmdline_user_args():
		push_error("Refusing visual smoke without --test-mode; real profiles must remain untouched.")
		quit(2)
		return
	if DisplayServer.get_name() == "headless":
		push_error("Visual smoke requires native rendering. Remove --headless.")
		quit(2)
		return
	_run.call_deferred()


func _run() -> void:
	started_ms = Time.get_ticks_msec()
	initial_user_files = _user_files_snapshot()
	var directory_result: Error = DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(ARTIFACT_DIR))
	_check(directory_result == OK or directory_result == ERR_ALREADY_EXISTS, "artifact directory is available")
	DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED)
	_resize(BASE_SIZE)
	game = GAME.new()
	game._test_mode = true
	root.add_child(game)
	game._test_mode = true
	game._set_mode("title")
	await _settle(2.0, 30)
	await _capture("title", BASE_SIZE)
	_check(game.mode == "title", "title screenshot uses the real title mode")
	_check(game.hud.get_child_count() > 0, "frontend contains rendered controls rather than the interface scaffold")

	game._start_game(false)
	await _settle(0.4, 10)
	game._enter_car(game.hero_car)
	var start_position: Vector3 = game.hero_car.position
	_key(KEY_W, true)
	await _settle(3.0, 60)
	_key(KEY_W, false)
	var travel_distance: float = game.hero_car.position.distance_to(start_position)
	_check(travel_distance > 8.0, "real keyboard input drives the hero car before gameplay capture")
	_key(KEY_SPACE, true)
	await _settle(1.4, 30)
	_key(KEY_SPACE, false)
	await _settle(0.3, 8)
	_check(absf(game.hero_car.speed) < 1.0, "hero car is stopped with the keyboard handbrake")
	performance_snapshot = {
		"fps": Engine.get_frames_per_second(),
		"objects_in_frame": Performance.get_monitor(Performance.RENDER_TOTAL_OBJECTS_IN_FRAME),
		"draw_calls_in_frame": Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME),
		"primitives_in_frame": Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME),
		"window_size": _size_array(root.size),
		"travel_distance": travel_distance,
		"note": "Single native test run; this is a snapshot, not a benchmark or hardware-controller verification.",
	}
	print("VISUAL_PERFORMANCE ", JSON.stringify(performance_snapshot))
	await _capture("game", BASE_SIZE)

	game._set_mode("paused")
	await _settle(0.4, 10)
	_check(paused and game.mode == "paused", "pause screenshot freezes the simulation")
	await _capture("pause", BASE_SIZE)
	game._ui_action("settings")
	await _settle(0.4, 10)
	_check(paused and game.mode == "settings", "settings opens through the real UI action handler")
	await _capture("settings", BASE_SIZE)
	game._ui_action("settings_back")
	game._ui_action("map")
	await _settle(0.4, 10)
	_check(paused and game.mode == "map", "full map screenshot uses the real map mode")
	await _capture("map", BASE_SIZE)
	game._set_waypoint(Vector3(-144.0, 0.0, -72.0))
	await _settle(0.3, 8)
	await _capture("map_waypoint", BASE_SIZE)
	game._set_mode("playing")
	game.time_of_day = 21.0
	game.city.set_time_of_day(21.0)
	await _settle(0.5, 12)
	await _capture("night", BASE_SIZE)
	game._set_mode("photo")
	await _settle(0.4, 10)
	_check(paused and game.mode == "photo", "photo mode pauses the world while rendering its controls")
	await _capture("photo_mode", BASE_SIZE)
	game.time_of_day = 17.6
	game.city.set_time_of_day(17.6)

	# Reuse the real scene while changing the native window, retaining the project's
	# canvas_items / 1600x900 content scale. Capture both HUD and menus at each size.
	for requested_size: Vector2i in [Vector2i(1280, 720), Vector2i(2560, 1440)]:
		_resize(requested_size)
		await _settle(0.7, 15)
		var suffix: String = "_%dx%d" % [requested_size.x, requested_size.y]
		game._set_mode("title")
		await _settle(0.4, 10)
		await _capture("title" + suffix, requested_size)
		game._set_mode("playing")
		await _settle(0.4, 10)
		await _capture("game" + suffix, requested_size)
		game._set_mode("paused")
		game._ui_action("settings")
		await _settle(0.4, 10)
		await _capture("settings" + suffix, requested_size)

	_check(_user_files_snapshot() == initial_user_files, "profile/settings and their backup/temp files are unchanged")
	await _finish()


func _resize(size: Vector2i) -> void:
	DisplayServer.window_set_size(size)
	root.size = size
	print("VISUAL_WINDOW requested=", size, " actual=", DisplayServer.window_get_size(), " content_scale=", root.content_scale_size, " scale_mode=", root.content_scale_mode)


func _settle(seconds: float, minimum_frames: int) -> void:
	var deadline: int = Time.get_ticks_msec() + int(seconds * 1000.0)
	var frames: int = 0
	while Time.get_ticks_msec() < deadline or frames < minimum_frames:
		await process_frame
		await RenderingServer.frame_post_draw
		frames += 1


func _capture(label: String, expected_size: Vector2i) -> void:
	await process_frame
	await RenderingServer.frame_post_draw
	var screenshot: Image = root.get_texture().get_image()
	if screenshot == null or screenshot.is_empty():
		_check(false, label + " produced a nonempty rendered image")
		return
	var actual_size: Vector2i = screenshot.get_size()
	_check(actual_size == expected_size, label + " image resolution is " + str(expected_size))
	var path: String = ARTIFACT_DIR.path_join(label + ".png")
	var result: Error = screenshot.save_png(path)
	_check(result == OK, label + " screenshot saves successfully")
	# Catch black/unrendered captures without pretending this is a visual design review.
	var sampled_colors: Dictionary = {}
	for y: int in range(9):
		for x: int in range(16):
			var pixel: Color = screenshot.get_pixel(int((float(x) + 0.5) * actual_size.x / 16.0), int((float(y) + 0.5) * actual_size.y / 9.0))
			sampled_colors[pixel.to_html(false)] = true
	_check(sampled_colors.size() >= 12, label + " contains a rendered scene with multiple visible colors")
	captures.append({"name": label, "path": path, "width": actual_size.x, "height": actual_size.y, "sampled_colors": sampled_colors.size(), "mode": game.mode})
	print("SCREENSHOT_SAVED ", ProjectSettings.globalize_path(path))


func _key(code: Key, pressed: bool) -> void:
	var event: InputEventKey = InputEventKey.new()
	event.physical_keycode = code
	event.keycode = code
	event.pressed = pressed
	Input.parse_input_event(event)


func _user_files_snapshot() -> Dictionary:
	var snapshot: Dictionary = {}
	for filename: String in ["profile.json", "settings.json"]:
		for suffix: String in ["", ".bak", ".tmp"]:
			var path: String = "user://" + filename + suffix
			var exists: bool = FileAccess.file_exists(path)
			snapshot[path] = {"exists": exists, "sha256": FileAccess.get_sha256(path) if exists else "", "modified": FileAccess.get_modified_time(path) if exists else 0}
	return snapshot


func _size_array(size: Vector2i) -> Array[int]:
	return [size.x, size.y]


func _check(condition: bool, message: String) -> void:
	if condition:
		print("PASS ", message)
	else:
		failures.append(message)
		push_error("FAIL " + message)


func _finish() -> void:
	_key(KEY_W, false)
	_key(KEY_SPACE, false)
	paused = false
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	var report: Dictionary = {
		"success": failures.is_empty(), "failures": failures, "captures": captures,
		"performance": performance_snapshot, "elapsed_ms": Time.get_ticks_msec() - started_ms,
		"profile_settings_unchanged": _user_files_snapshot() == initial_user_files,
		"physical_controller_verified": false,
	}
	var report_file: FileAccess = FileAccess.open(ARTIFACT_DIR.path_join("visual_smoke.json"), FileAccess.WRITE)
	if report_file != null:
		report_file.store_string(JSON.stringify(report, "\t") + "\n")
		report_file.close()
	else:
		_check(false, "visual smoke JSON report is writable")
	game.queue_free()
	await process_frame
	await process_frame
	await create_timer(0.10).timeout
	print("VISUAL_SMOKE_COMPLETE captures=", captures.size(), " failures=", failures.size(), " | physical_controller_verified=false")
	quit(0 if failures.is_empty() else 1)

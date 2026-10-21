extends SceneTree
## Integration test with synthetic input only; no physical controller is claimed.
## Godot --headless --path . --fixed-fps 60 --script res://tests/game_smoke.gd -- --test-mode
const GAME = preload("res://scripts/game.gd")
var game: Node3D
var failures: Array[String] = []

func _initialize() -> void:
	if not "--test-mode" in OS.get_cmdline_user_args():
		push_error("Refusing game integration test without --test-mode; real profiles must remain untouched.")
		quit(2)
		return
	_run.call_deferred()

func _check(condition: bool, message: String) -> void:
	if condition:
		print("PASS ", message)
	else:
		failures.append(message)
		push_error("FAIL " + message)

func _frames(count: int) -> void:
	for frame: int in count:
		await physics_frame

func _key(code: Key, pressed: bool) -> void:
	var event: InputEventKey = InputEventKey.new()
	event.physical_keycode = code
	event.keycode = code
	event.pressed = pressed
	Input.parse_input_event(event)

func _pad_button(button: JoyButton, pressed: bool) -> void:
	var event: InputEventJoypadButton = InputEventJoypadButton.new()
	event.device = 15
	event.button_index = button
	event.pressed = pressed
	Input.parse_input_event(event)

func _pad_axis(axis: JoyAxis, value: float) -> void:
	var event: InputEventJoypadMotion = InputEventJoypadMotion.new()
	event.device = 15
	event.axis = axis
	event.axis_value = value
	Input.parse_input_event(event)

func _blocked(at: Vector3, radius: float = 0.25) -> bool:
	var query: PhysicsShapeQueryParameters3D = PhysicsShapeQueryParameters3D.new()
	var shape: SphereShape3D = SphereShape3D.new()
	shape.radius = radius
	query.shape = shape
	query.transform = Transform3D(Basis.IDENTITY, at + Vector3.UP)
	query.collision_mask = 1
	return not game.world.get_world_3d().direct_space_state.intersect_shape(query, 1).is_empty()

func _run() -> void:
	game = GAME.new()
	root.add_child(game)
	game._test_mode = true
	game._start_game(false)
	await _frames(45)
	print("DIAGNOSTIC connected_controller_ids=", Input.get_connected_joypads())
	_check(game.mode == "playing" and game.cars.size() == 17 and game.pedestrians.size() == 32, "full world starts with player traffic and pedestrians")
	_check(not _blocked(game.player.position) and game.player.position.y > -0.1, "protagonist starts clear of buildings and on solid ground")
	_check(is_instance_valid(game.near_vehicle) and game.near_vehicle == game.hero_car, "initial vehicle is within interaction range")
	_pad_button(JOY_BUTTON_Y, true)
	await _frames(2)
	_pad_button(JOY_BUTTON_Y, false)
	_check(game.current_car == game.hero_car and game.player.seated and game.using_controller, "synthetic controller Y enters the nearby car and selects controller hints")
	_check(game.missions.stage == 1, "entering a vehicle advances the first mission")
	var initial_z: float = game.hero_car.position.z
	_pad_axis(JOY_AXIS_TRIGGER_RIGHT, 1.0)
	await _frames(150)
	_check(game.hero_car.speed > 12.0 and game.hero_car.position.z < initial_z - 15.0, "synthetic controller trigger drives through the real world")
	_pad_axis(JOY_AXIS_TRIGGER_RIGHT, 0.0)
	_pad_button(JOY_BUTTON_B, true)
	await _frames(150)
	_pad_button(JOY_BUTTON_B, false)
	_check(absf(game.hero_car.speed) < 1.0, "synthetic controller B handbrake stops the car")
	var light_state: bool = game.hero_car.lights_on
	_key(KEY_L, true)
	await _frames(1)
	_key(KEY_L, false)
	_check(game.hero_car.lights_on != light_state and not game.using_controller, "keyboard headlights action works and restores keyboard hints")
	_key(KEY_F, true)
	await _frames(2)
	_key(KEY_F, false)
	_check(not is_instance_valid(game.current_car) and not game.player.seated and game.player.visible, "keyboard interaction exits a stopped car")
	_check(not _blocked(game.player.position), "normal vehicle exit produces a clear on-foot position")
	var before: Vector3 = game.player.position
	_key(KEY_W, true)
	await _frames(60)
	_key(KEY_W, false)
	await _frames(12)
	_check(game.player.position.distance_to(before) > 3.0, "keyboard on-foot movement passes through the game input loop")
	_pad_button(JOY_BUTTON_START, true)
	await _frames(2)
	_pad_button(JOY_BUTTON_START, false)
	var paused_position: Vector3 = game.player.position
	await _frames(30)
	_check(game.mode == "paused" and paused and game.player.position.distance_to(paused_position) < 0.01, "controller Menu pauses simulation")
	_pad_button(JOY_BUTTON_START, true)
	await _frames(2)
	_pad_button(JOY_BUTTON_START, false)
	_check(game.mode == "playing" and not paused, "controller Menu resumes simulation")
	_key(KEY_M, true)
	await _frames(2)
	_key(KEY_M, false)
	_check(game.mode == "map" and paused, "keyboard map opens with simulation paused")
	game._set_waypoint(Vector3(31.0, 0.0, 99.0))
	_check(game.waypoint_active and (is_zero_approx(fmod(game.waypoint.x, 72.0)) or is_zero_approx(fmod(game.waypoint.z, 72.0))), "map waypoint snaps to a traversable road")
	game._set_mode("playing")
	game._enter_car(game.hero_car)
	game.hero_car.take_damage(50.0)
	var prior_cash: int = game.cash
	game._recover()
	_check(game.hero_car.health == 100.0 and game.cash == maxi(0, prior_cash - 75), "roadside recovery repairs the vehicle and charges the documented fee")
	game._raise_heat(48.0)
	await _frames(10)
	_check(game.wanted == 2 and game.patrols.size() == 2, "two wanted stars create two pursuing patrols")
	var valid_patrols: bool = true
	for patrol: CharacterBody3D in game.patrols:
		valid_patrols = valid_patrols and patrol.npc_driving and patrol.is_police and not _blocked(patrol.position)
	_check(valid_patrols, "patrols spawn on clear roads with active pursuit controllers")
	game.heat = 120.0
	game.wanted = 5
	await _frames(2)
	var pursuit_car: CharacterBody3D = game.patrols[0]
	for index: int in range(1, game.patrols.size()):
		game.patrols[index].reset_at(Vector3(-216.0, 0.2, -216.0 + index * 12.0), 0.0)
	game.hero_car.reset_at(Vector3(216.0, 0.3, 0.0), 0.0)
	pursuit_car.reset_at(Vector3(216.0, 0.3, 100.0), 0.0)
	var pursuit_start: float = pursuit_car.position.distance_to(game.hero_car.position)
	await _frames(360)
	print("DIAGNOSTIC straight_pursuit_distance=", pursuit_start, " -> ", pursuit_car.position.distance_to(game.hero_car.position))
	_check(pursuit_car.position.distance_to(game.hero_car.position) < pursuit_start - 25.0 and not _blocked(pursuit_car.position), "patrol closes distance along a straight avenue without entering world geometry")
	game.hero_car.reset_at(Vector3(144.0, 0.3, 144.0), 0.0)
	pursuit_car.reset_at(Vector3(72.0, 0.3, 0.0), -PI / 2.0)
	pursuit_start = pursuit_car.position.distance_to(game.hero_car.position)
	game.heat = 120.0
	game.wanted = 5
	await _frames(720)
	print("DIAGNOSTIC corner_pursuit_distance=", pursuit_start, " -> ", pursuit_car.position.distance_to(game.hero_car.position), " position=", pursuit_car.position, " speed=", pursuit_car.speed)
	_check(pursuit_car.position.distance_to(game.hero_car.position) < pursuit_start - 30.0 and not _blocked(pursuit_car.position), "patrol follows grid roads and closes distance through a junction")
	game._exit_car()
	for index: int in range(1, game.patrols.size()):
		game.patrols[index].reset_at(Vector3(-216.0, 0.2, -216.0 + index * 12.0), 0.0)
	game._enter_car(pursuit_car)
	var theft_player_health: float = game.player.health
	var theft_vehicle_health: float = pursuit_car.health
	await _frames(3)
	_check(not game.patrols.has(pursuit_car) and game.player.health >= theft_player_health and pursuit_car.health >= theft_vehicle_health, "stealing a patrol removes its police AI and prevents self-pursuit damage")
	game._joy_connection(15, false)
	_check(game.mode == "paused" and not game.using_controller and game.active_pad == -1, "disconnecting the active controller pauses safely")
	game._set_mode("playing")
	game.heat = 0.0
	game.wanted = 0
	game._exit_car()
	# Deterministic geometry diagnostics for all sidewalk walkers and traffic.
	var pedestrians_blocked: int = 0
	for person: Node3D in game.pedestrians:
		if _blocked(person.position):
			pedestrians_blocked += 1
			print("DIAGNOSTIC blocked_pedestrian seed=", person.seed_value, " origin=", person.origin, " position=", person.position)
	print("DIAGNOSTIC pedestrians_inside_world_geometry=", pedestrians_blocked)
	game.player.reset_at(Vector3(280.0, 0.4, 30.0))
	await _frames(45)
	_check(game.player.is_on_floor() and game.player.position.y > 0.0 and game.player.position.y < 0.2, "player can stand on the beach beyond the city ground boundary")
	game.camera_yaw = 0.0
	_key(KEY_D, true)
	await _frames(90)
	_key(KEY_D, false)
	_check(game.player.position.x > 286.0 and game.player.position.y > -0.1, "on-foot controls traverse the solid beach without the old car boundary")
	# Park beside a real generated building, with the driver's old +X save
	# offset inside the wall. Exercise the shared helper without writing files.
	var wall_parking: Vector3 = Vector3.ZERO
	var wall_case_found: bool = false
	for block: Dictionary in game.city.get_map_blocks():
		var candidate: Vector3 = Vector3(float(block.x) - float(block.w) * 0.5 - 1.3, 0.3, float(block.z))
		if not _blocked(candidate) and _blocked(candidate + Vector3(3.0, 0.0, 0.0)):
			wall_parking = candidate
			wall_case_found = true
			break
	game.hero_car.reset_at(wall_parking, 0.0)
	game._enter_car(game.hero_car)
	await _frames(5)
	var unsafe_old_destination: Vector3 = game.hero_car.position + game.hero_car.basis.x * 3.0
	_check(wall_case_found and not _blocked(game.hero_car.position) and _blocked(unsafe_old_destination), "regression fixture parks a clear car beside a building that blocks the old save offset")
	var safe_destination: Vector3 = game._safe_exit_position(game.hero_car)
	_check(not _blocked(safe_destination) and safe_destination.distance_to(game.hero_car.position) < 7.0, "shared exit and save helper chooses a clear nearby capsule destination")
	var corrected_load: Vector3 = game._safe_foot_position(unsafe_old_destination)
	_check(not _blocked(corrected_load), "load-position helper recovers a saved point inside a building")
	game._exit_car()
	await _frames(40)
	_check(not _blocked(game.player.position) and game.player.is_on_floor() and game.player.position.distance_to(safe_destination) < 0.25, "vehicle exit uses the safe destination and player settles on solid ground")
	var cars_moving: int = 0
	await _frames(1200)
	for car: CharacterBody3D in game.cars:
		if car.npc_driving and absf(car.speed) > 2.0: cars_moving += 1
	print("DIAGNOSTIC moving_traffic_after_long_run=", cars_moving, "/12")
	_check(game.camera.position.is_finite() and game.camera.position.y > 0.1, "camera remains finite and above the floor during simulation")
	# Restart while driving the stolen patrol and with a defeated pedestrian.
	var old_person: Node3D = game.pedestrians[0]
	old_person.take_damage(100.0)
	game._enter_car(pursuit_car)
	game._start_game(false)
	await _frames(4)
	var people_restored: bool = true
	for person: Node3D in game.pedestrians:
		people_restored = people_restored and is_equal_approx(person.health, 100.0)
	_check(game.cars.size() == 17 and game.pedestrians.size() == 32 and game.patrols.is_empty() and people_restored, "restart restores standard populations and defeated pedestrians")
	_check(not is_instance_valid(pursuit_car) and not is_instance_valid(old_person) and not is_instance_valid(game.current_car) and not game.player.seated, "restart frees the stolen patrol and clears its driver references")
	var traffic_links_valid: bool = game.traffic_routes.size() == 12
	for id: int in game.traffic_routes:
		var linked_car: bool = false
		for car: CharacterBody3D in game.cars:
			if car.get_instance_id() == id and car.npc_driving and not car.controlled:
				linked_car = true
		traffic_links_valid = traffic_links_valid and linked_car
	_check(traffic_links_valid and game.near_vehicle == game.hero_car and not game.hero_car.controlled and not game.hero_car.driver_present, "restart rebuilds traffic routes and restores an unoccupied nearby hero car")
	var borrowed_car: CharacterBody3D = game.cars[game.cars.size() - 1]
	borrowed_car.reset_at(Vector3(216.0, 0.3, -140.0), 0.4)
	game._enter_car(borrowed_car)
	game._exit_car()
	var foot_snapshot: Dictionary = game._save_state()
	_check(game.last_used_car == borrowed_car and foot_snapshot.car_position == borrowed_car.position and foot_snapshot.car_yaw == borrowed_car.rotation.y and foot_snapshot.position == game.player.position, "on-foot save snapshot preserves the last driven vehicle location and heading")
	game._recover()
	_check(game.last_used_car == game.hero_car and game._save_state().car_position == game.hero_car.position, "on-foot recovery selects the recovered hero car for subsequent saves")
	game._enter_car(borrowed_car)
	game._exit_car()
	game._respawn()
	_check(game.last_used_car == game.hero_car and game._save_state().car_position == game.hero_car.position, "respawn selects the restored hero car for subsequent saves")
	var profile_before_reset: Dictionary = {}
	for suffix: String in ["", ".bak", ".tmp"]:
		var profile_path: String = "user://profile.json" + suffix
		profile_before_reset[suffix] = FileAccess.get_sha256(profile_path) if FileAccess.file_exists(profile_path) else "missing"
	game._ui_action("reset_save")
	game._save()
	var profiles_preserved: bool = true
	for suffix: String in ["", ".bak", ".tmp"]:
		var profile_path: String = "user://profile.json" + suffix
		var after_reset: String = FileAccess.get_sha256(profile_path) if FileAccess.file_exists(profile_path) else "missing"
		profiles_preserved = profiles_preserved and after_reset == profile_before_reset[suffix]
	_check(game.saving_suspended and not game.has_save and game.has_started and game.mode == "title" and profiles_preserved, "reset suspends future saves and returns to title while test mode preserves all real profile files")
	game._start_game(false)
	await _frames(3)
	_check(not game.saving_suspended and game.last_used_car == game.hero_car and game.cars.size() == 17, "starting after a reset reenables saves and rebuilds the population with a valid remembered vehicle")
	print("GAME_SMOKE_COMPLETE failures=", failures.size(), " | physical_controller_verified=false")
	paused = false
	game.queue_free()
	await process_frame
	# --fixed-fps runs faster than the audio mixing thread; allow stopped WAV
	# playbacks to drain after freeing the scene before shutting the server down.
	OS.delay_msec(80)
	await process_frame
	quit(0 if failures.is_empty() else 1)

extends SceneTree
## Run: Godot --headless --path . --fixed-fps 120 --script res://tests/actors_smoke.gd

const CAR = preload("res://scripts/car.gd")
const PLAYER = preload("res://scripts/player.gd")
const PEDESTRIAN = preload("res://scripts/pedestrian.gd")
var failures: Array[String] = []
var world: Node3D

func _initialize() -> void:
	Engine.physics_ticks_per_second = 120
	_run.call_deferred()

func _assert_check(condition: bool, message: String) -> void:
	if condition:
		print("PASS ", message)
	else:
		failures.append(message)
		push_error("FAIL " + message)

func _frames(count: int) -> void:
	for frame: int in count:
		await physics_frame

func _solid(size: Vector3, position: Vector3) -> void:
	var body: StaticBody3D = StaticBody3D.new()
	var collision: CollisionShape3D = CollisionShape3D.new()
	var shape: BoxShape3D = BoxShape3D.new()
	shape.size = size
	collision.shape = shape
	body.position = position
	body.add_child(collision)
	world.add_child(body)

func _run() -> void:
	world = Node3D.new()
	root.add_child(world)
	_solid(Vector3(500.0, 1.0, 500.0), Vector3(0.0, -0.5, 0.0))
	_solid(Vector3(12.0, 4.0, 1.0), Vector3(0.0, 2.0, -40.0))
	var car = CAR.new()
	car.position = Vector3(0.0, 0.1, 0.0)
	car.controlled = true
	world.add_child(car)
	car.throttle = 1.0
	await _frames(180)
	_assert_check(car.speed > 12.0 and car.global_position.z < -8.0, "car accelerates forward with injected throttle")
	_assert_check(car.global_position.y > -0.1 and car.global_position.y < 0.1, "car rests with tires at the floor")
	car.throttle = 0.0
	car.braking = true
	await _frames(160)
	_assert_check(absf(car.speed) < 0.5, "handbrake stops the car")
	car.reset_at(Vector3(0.0, 0.1, 0.0), 0.0)
	car.braking = false
	car.throttle = 1.0
	await _frames(600)
	print("wall_probe position=", car.global_position, " speed=", car.speed, " health=", car.health)
	_assert_check(car.global_position.z > -38.0, "building collision prevents vehicle tunneling")
	_assert_check(car.health < 100.0, "vehicle collision applies impact damage")
	car.repair()
	_assert_check(is_equal_approx(car.health, 100.0), "repair restores condition")
	car.reset_at(Vector3(40.0, 0.1, 0.0), 0.0)
	car.throttle = -1.0
	await _frames(180)
	_assert_check(car.speed < -5.0 and car.global_position.z > 3.0, "reverse travels backward")
	car.throttle = 0.0
	car.braking = true
	var player = PLAYER.new()
	player.position = Vector3(20.0, 0.1, 0.0)
	world.add_child(player)
	await _frames(25)
	player.move_input = Vector2(0.0, -1.0)
	await _frames(90)
	_assert_check(player.global_position.z < -2.0, "player moves camera-relative forward")
	player.move_input = Vector2.ZERO
	player.jump_requested = true
	await _frames(28)
	_assert_check(player.global_position.y > 0.5, "player jump leaves the ground")
	await _frames(120)
	_assert_check(player.global_position.y < 0.1, "player lands on the world collider")
	player.set_seated(true)
	await _frames(3)
	_assert_check(not player.visible and player.collision_layer == 0 and player.collision_mask == 0, "seated player is hidden and cannot collide with the vehicle")
	player.reset_at(Vector3(15.0, 0.1, 0.0))
	await _frames(3)
	_assert_check(player.visible and player.collision_layer == 4 and is_equal_approx(player.health, 100.0), "player reset restores visibility collision and health")
	var pedestrian = PEDESTRIAN.new()
	pedestrian.seed_value = 31
	pedestrian.origin = Vector3(30.0, 0.0, 30.0)
	pedestrian.position = pedestrian.origin
	world.add_child(pedestrian)
	pedestrian.set_walk_axis(Vector3.RIGHT, 5.0)
	pedestrian.start_panic(Vector3(20.0, 0.0, 30.0))
	await _frames(100)
	_assert_check(absf(pedestrian.global_position.z - 30.0) < 0.01 and absf(pedestrian.global_position.x - 30.0) <= 5.01, "panicking pedestrian stays on its assigned sidewalk route")
	pedestrian.take_damage(100.0)
	await _frames(3)
	_assert_check(pedestrian.health == 0.0, "pedestrian accepts damage")
	print("ACTOR_SMOKE_COMPLETE failures=", failures.size())
	quit(0 if failures.is_empty() else 1)

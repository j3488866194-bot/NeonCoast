extends CharacterBody3D
## Procedural on-foot protagonist; input and camera remain in the game controller.

var move_input: Vector2 = Vector2.ZERO
var view_yaw: float = 0.0
var sprinting: bool = false
var jump_requested: bool = false
var aiming: bool = false
var health: float = 100.0
var seated: bool = false

var _visual: Node3D
var _left_leg: Node3D
var _right_leg: Node3D
var _left_arm: Node3D
var _right_arm: Node3D
var _left_forearm: Node3D
var _right_forearm: Node3D
var _weapon: Node3D
var _collider: CollisionShape3D
var _gait: float = 0.0
var _hurt_flash: float = 0.0
var _shirt: StandardMaterial3D

func _ready() -> void:
	collision_layer = 4
	collision_mask = 3
	floor_snap_length = 0.3
	floor_max_angle = deg_to_rad(48.0)
	_collider = CollisionShape3D.new()
	var shape: CapsuleShape3D = CapsuleShape3D.new()
	shape.radius = 0.30
	shape.height = 1.82
	_collider.shape = shape
	_collider.position.y = 0.91
	add_child(_collider)
	_build_person()
	add_to_group("player")

func _physics_process(delta: float) -> void:
	if seated:
		velocity = Vector3.ZERO
		jump_requested = false
		return
	var input: Vector2 = move_input.limit_length(1.0)
	var direction: Vector3 = Basis(Vector3.UP, view_yaw) * Vector3(input.x, 0.0, input.y)
	var pace: float = 3.3 if aiming else (8.7 if sprinting else 5.2)
	if health <= 0.0:
		pace = 0.0
	var desired: Vector3 = direction * pace
	velocity.x = move_toward(velocity.x, desired.x, 31.0 * delta)
	velocity.z = move_toward(velocity.z, desired.z, 31.0 * delta)
	if is_on_floor():
		velocity.y = -0.5
		if jump_requested and health > 0.0:
			velocity.y = 7.2
	else:
		velocity.y -= 22.0 * delta
	jump_requested = false
	move_and_slide()
	global_position.x = clampf(global_position.x, -246.0, 308.0)
	global_position.z = clampf(global_position.z, -246.0, 246.0)
	if global_position.y < -8.0:
		global_position.y = 2.0
		velocity = Vector3.ZERO
	var horizontal_speed: float = Vector2(velocity.x, velocity.z).length()
	if aiming:
		_visual.rotation.y = lerp_angle(_visual.rotation.y, view_yaw, minf(1.0, delta * 15.0))
	elif direction.length_squared() > 0.01:
		var target_yaw: float = atan2(-direction.x, -direction.z)
		_visual.rotation.y = lerp_angle(_visual.rotation.y, target_yaw, minf(1.0, delta * 12.0))
	_animate(delta, horizontal_speed)

func take_damage(amount: float) -> void:
	health = clampf(health - maxf(0.0, amount), 0.0, 100.0)
	_hurt_flash = 0.2

func reset_at(pos: Vector3) -> void:
	global_position = pos
	velocity = Vector3.ZERO
	health = 100.0
	set_seated(false)

func set_seated(value: bool) -> void:
	seated = value
	visible = not value
	velocity = Vector3.ZERO
	if is_instance_valid(_collider):
		_collider.set_deferred("disabled", value)
	collision_layer = 0 if value else 4
	collision_mask = 0 if value else 3

func _animate(delta: float, horizontal_speed: float) -> void:
	var amount: float = clampf(horizontal_speed / 5.2, 0.0, 1.3)
	_gait += delta * horizontal_speed * 2.0
	var gait_angle: float = sin(_gait) * 0.62 * amount if is_on_floor() else 0.18
	_left_leg.rotation.x = lerpf(_left_leg.rotation.x, gait_angle, minf(delta * 15.0, 1.0))
	_right_leg.rotation.x = lerpf(_right_leg.rotation.x, -gait_angle, minf(delta * 15.0, 1.0))
	_visual.position.y = absf(cos(_gait)) * 0.045 * amount if is_on_floor() else 0.0
	_visual.rotation.x = lerpf(_visual.rotation.x, -0.055 * amount, delta * 8.0)
	_left_arm.rotation.x = lerpf(_left_arm.rotation.x, 1.18 if aiming else -gait_angle * 0.82, minf(delta * 14.0, 1.0))
	_right_arm.rotation.x = lerpf(_right_arm.rotation.x, 1.38 if aiming else gait_angle * 0.82, minf(delta * 14.0, 1.0))
	_left_arm.rotation.z = lerpf(_left_arm.rotation.z, -0.34 if aiming else 0.07, delta * 10.0)
	_right_arm.rotation.z = lerpf(_right_arm.rotation.z, 0.10 if aiming else -0.07, delta * 10.0)
	_left_forearm.rotation.x = lerpf(_left_forearm.rotation.x, 0.42 if aiming else 0.16 + amount * 0.3, delta * 10.0)
	_right_forearm.rotation.x = lerpf(_right_forearm.rotation.x, 0.14 if aiming else 0.16 + amount * 0.3, delta * 10.0)
	_weapon.visible = aiming
	_hurt_flash = maxf(_hurt_flash - delta, 0.0)
	_shirt.albedo_color = Color("eab6aa") if _hurt_flash > 0.0 else Color("1ab5b4")

func _material(color: Color, roughness: float = 0.8) -> StandardMaterial3D:
	var material: StandardMaterial3D = StandardMaterial3D.new()
	material.albedo_color = color
	material.roughness = roughness
	return material

func _box(parent: Node3D, dimensions: Vector3, at: Vector3, material: Material) -> MeshInstance3D:
	var instance: MeshInstance3D = MeshInstance3D.new()
	var shape: BoxMesh = BoxMesh.new()
	shape.size = dimensions
	instance.mesh = shape
	instance.material_override = material
	instance.position = at
	parent.add_child(instance)
	return instance

func _sphere(parent: Node3D, radius: float, height: float, at: Vector3, material: Material) -> MeshInstance3D:
	var instance: MeshInstance3D = MeshInstance3D.new()
	var shape: SphereMesh = SphereMesh.new()
	shape.radius = radius
	shape.height = height
	shape.radial_segments = 12
	shape.rings = 6
	instance.mesh = shape
	instance.material_override = material
	instance.position = at
	parent.add_child(instance)
	return instance

func _build_person() -> void:
	_visual = Node3D.new()
	_visual.name = "Protagonist"
	add_child(_visual)
	var skin: StandardMaterial3D = _material(Color("bd805f"))
	var hair: StandardMaterial3D = _material(Color("23232b"))
	var pants: StandardMaterial3D = _material(Color("233243"))
	var shoe: StandardMaterial3D = _material(Color("f1e9d7"))
	var dark: StandardMaterial3D = _material(Color("122331"))
	_shirt = _material(Color("1ab5b4"))
	_box(_visual, Vector3(0.51, 0.58, 0.28), Vector3(0.0, 1.18, 0.0), _shirt)
	_box(_visual, Vector3(0.18, 0.52, 0.018), Vector3(0.0, 1.16, -0.151), dark)
	_box(_visual, Vector3(0.035, 0.51, 0.022), Vector3(-0.12, 1.17, -0.157), shoe)
	_box(_visual, Vector3(0.035, 0.51, 0.022), Vector3(0.12, 1.17, -0.157), shoe)
	_box(_visual, Vector3(0.48, 0.19, 0.28), Vector3(0.0, 0.82, 0.0), pants)
	_box(_visual, Vector3(0.49, 0.04, 0.29), Vector3(0.0, 0.91, 0.0), dark)
	_box(_visual, Vector3(0.065, 0.05, 0.02), Vector3(0.0, 0.91, -0.153), shoe)
	_sphere(_visual, 0.08, 0.19, Vector3(0.0, 1.50, 0.0), skin)
	var head: MeshInstance3D = _sphere(_visual, 0.19, 0.43, Vector3(0.0, 1.69, -0.005), skin)
	head.scale.x = 0.89
	_sphere(_visual, 0.193, 0.25, Vector3(0.0, 1.81, 0.015), hair)
	_box(_visual, Vector3(0.30, 0.14, 0.09), Vector3(0.0, 1.72, 0.14), hair)
	_box(_visual, Vector3(0.32, 0.065, 0.035), Vector3(0.0, 1.72, -0.177), dark)
	_box(_visual, Vector3(0.045, 0.065, 0.055), Vector3(0.0, 1.66, -0.189), skin)
	_box(_visual, Vector3(0.10, 0.018, 0.012), Vector3(0.0, 1.60, -0.17), hair)
	for side: float in [-1.0, 1.0]:
		_sphere(_visual, 0.046, 0.095, Vector3(side * 0.174, 1.68, 0.0), skin)
		var leg: Node3D = Node3D.new()
		leg.position = Vector3(side * 0.14, 0.80, 0.0)
		_visual.add_child(leg)
		_box(leg, Vector3(0.205, 0.35, 0.235), Vector3(0.0, -0.16, 0.0), pants)
		_box(leg, Vector3(0.18, 0.32, 0.205), Vector3(0.0, -0.49, 0.015), pants)
		_box(leg, Vector3(0.215, 0.125, 0.35), Vector3(0.0, -0.72, -0.055), shoe)
		_box(leg, Vector3(0.22, 0.035, 0.355), Vector3(0.0, -0.785, -0.055), dark)
		var arm: Node3D = Node3D.new()
		arm.position = Vector3(side * 0.305, 1.41, 0.0)
		_visual.add_child(arm)
		_box(arm, Vector3(0.18, 0.25, 0.23), Vector3(0.0, -0.10, 0.0), _shirt)
		_box(arm, Vector3(0.135, 0.18, 0.16), Vector3(0.0, -0.26, 0.0), skin)
		var forearm: Node3D = Node3D.new()
		forearm.position.y = -0.34
		arm.add_child(forearm)
		_box(forearm, Vector3(0.125, 0.24, 0.145), Vector3(0.0, -0.10, 0.0), skin)
		_box(forearm, Vector3(0.13, 0.12, 0.135), Vector3(0.0, -0.27, -0.01), skin)
		if side < 0.0:
			_left_leg = leg
			_left_arm = arm
			_left_forearm = forearm
			_box(forearm, Vector3(0.14, 0.045, 0.16), Vector3(0.0, -0.20, 0.0), dark)
		else:
			_right_leg = leg
			_right_arm = arm
			_right_forearm = forearm
	_weapon = Node3D.new()
	_weapon.position = Vector3(0.0, -0.27, -0.01)
	_weapon.rotation.x = -PI / 2.0
	_right_forearm.add_child(_weapon)
	_box(_weapon, Vector3(0.072, 0.10, 0.29), Vector3(0.0, 0.045, -0.11), _material(Color("505c64"), 0.25))
	_box(_weapon, Vector3(0.062, 0.13, 0.075), Vector3(0.0, -0.04, -0.03), dark)
	_box(_weapon, Vector3(0.05, 0.055, 0.018), Vector3(0.0, 0.044, -0.264), dark)
	_weapon.visible = false

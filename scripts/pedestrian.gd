extends Node3D
## Lightweight sidewalk pedestrian. Origin and seed are injected before _ready.

var origin: Vector3 = Vector3.ZERO
var seed_value: int = 0
var panic: float = 0.0
var health: float = 100.0
var _rng: RandomNumberGenerator = RandomNumberGenerator.new()
var _visual: Node3D
var _left_leg: Node3D
var _right_leg: Node3D
var _left_arm: Node3D
var _right_arm: Node3D
var _walk_axis: Vector3 = Vector3.FORWARD
var _walk_phase: float = 0.0
var _walk_distance: float = 0.0
var _direction: float = 1.0
var _gait: float = 0.0
var _pause: float = 0.0
var _panic_from: Vector3 = Vector3.ZERO
var _base_pace: float = 1.0
var _fallen: bool = false

func _ready() -> void:
	_rng.seed = seed_value + 6841
	if origin == Vector3.ZERO:
		origin = global_position
	_walk_axis = Vector3.RIGHT if seed_value % 2 == 0 else Vector3.FORWARD
	_walk_distance = _rng.randf_range(4.0, 9.0)
	_walk_phase = _rng.randf_range(-_walk_distance, _walk_distance)
	_direction = 1.0 if seed_value % 3 == 0 else -1.0
	_base_pace = _rng.randf_range(0.7, 1.25)
	_build_person()
	add_to_group("pedestrians")

func _process(delta: float) -> void:
	if health <= 0.0:
		if not _fallen:
			_fallen = true
		var down: float = PI * 0.48
		_visual.rotation.z = lerpf(_visual.rotation.z, down, delta * 5.0)
		_visual.position.y = lerpf(_visual.position.y, 0.15, delta * 5.0)
		return
	panic = maxf(0.0, panic - delta)
	_pause = maxf(0.0, _pause - delta)
	if _pause > 0.0 and panic <= 0.0:
		return
	var pace: float = _base_pace * (3.1 if panic > 0.0 else 1.0)
	_walk_phase += _direction * pace * delta
	if absf(_walk_phase) > _walk_distance:
		_walk_phase = clampf(_walk_phase, -_walk_distance, _walk_distance)
		_direction *= -1.0
		if panic <= 0.0:
			_pause = _rng.randf_range(0.1, 2.3)
	# A bounded line along one sidewalk avoids unconstrained wander into buildings.
	var proposed: Vector3 = origin + _walk_axis * _walk_phase
	proposed.y = origin.y
	global_position = proposed
	var facing: Vector3 = _walk_axis * _direction
	_visual.rotation.y = lerp_angle(_visual.rotation.y, atan2(-facing.x, -facing.z), minf(delta * 7.0, 1.0))
	_gait += pace * delta * 5.5
	var swing: float = sin(_gait) * (0.70 if panic > 0.0 else 0.35)
	_left_leg.rotation.x = swing
	_right_leg.rotation.x = -swing
	_left_arm.rotation.x = -swing * 0.8
	_right_arm.rotation.x = swing * 0.8
	_visual.position.y = absf(cos(_gait)) * 0.035

func set_walk_axis(axis: Vector3, distance: float = 7.0) -> void:
	axis.y = 0.0
	if axis.length_squared() > 0.01:
		_walk_axis = axis.normalized()
	_walk_distance = maxf(1.0, distance)
	_walk_phase = clampf(_walk_phase, -_walk_distance, _walk_distance)

func take_damage(amount: float) -> void:
	health = clampf(health - maxf(0.0, amount), 0.0, 100.0)
	panic = maxf(panic, 7.0)

func start_panic(from: Vector3) -> void:
	panic = 6.0
	_panic_from = from
	var away: Vector3 = global_position - from
	_direction = 1.0 if away.dot(_walk_axis) >= 0.0 else -1.0
	_pause = 0.0

func _material(color: Color) -> StandardMaterial3D:
	var material: StandardMaterial3D = StandardMaterial3D.new()
	material.albedo_color = color
	material.roughness = 0.88
	return material

func _box(parent: Node3D, size: Vector3, at: Vector3, material: Material) -> void:
	var instance: MeshInstance3D = MeshInstance3D.new()
	var shape: BoxMesh = BoxMesh.new()
	shape.size = size
	instance.mesh = shape
	instance.material_override = material
	instance.position = at
	parent.add_child(instance)

func _build_person() -> void:
	_visual = Node3D.new()
	add_child(_visual)
	var shirts: Array[Color] = [Color("f2ae66"), Color("df5b85"), Color("49bcba"), Color("dce0e1"), Color("9188cc"), Color("8fb26e")]
	var skins: Array[Color] = [Color("dba980"), Color("a86e50"), Color("754a39"), Color("ecc8a1")]
	var shirt: StandardMaterial3D = _material(shirts[posmod(seed_value, shirts.size())])
	var skin: StandardMaterial3D = _material(skins[posmod(seed_value / 2, skins.size())])
	var trousers: StandardMaterial3D = _material(Color("354052") if seed_value % 2 == 0 else Color("6c655d"))
	var hair: StandardMaterial3D = _material(Color("342a2a") if seed_value % 3 else Color("b49b68"))
	var shoes: StandardMaterial3D = _material(Color("e1ddd3"))
	_box(_visual, Vector3(0.44, 0.56, 0.25), Vector3(0.0, 1.10, 0.0), shirt)
	_box(_visual, Vector3(0.40, 0.16, 0.25), Vector3(0.0, 0.78, 0.0), trousers)
	_box(_visual, Vector3(0.12, 0.13, 0.13), Vector3(0.0, 1.42, 0.0), skin)
	var head: MeshInstance3D = MeshInstance3D.new()
	var sphere: SphereMesh = SphereMesh.new()
	sphere.radius = 0.165
	sphere.height = 0.38
	sphere.radial_segments = 10
	sphere.rings = 5
	head.mesh = sphere
	head.material_override = skin
	head.position = Vector3(0.0, 1.61, 0.0)
	_visual.add_child(head)
	_box(_visual, Vector3(0.29, 0.10, 0.28), Vector3(0.0, 1.77, 0.02), hair)
	_box(_visual, Vector3(0.10, 0.025, 0.02), Vector3(0.0, 1.57, -0.16), hair)
	if seed_value % 3 == 0:
		_box(_visual, Vector3(0.29, 0.045, 0.025), Vector3(0.0, 1.66, -0.153), _material(Color("222a37")))
	if seed_value % 4 == 0:
		_box(_visual, Vector3(0.33, 0.16, 0.27), Vector3(0.0, 1.47, 0.09), hair)
	for side: float in [-1.0, 1.0]:
		var leg: Node3D = Node3D.new()
		leg.position = Vector3(side * 0.12, 0.74, 0.0)
		_visual.add_child(leg)
		_box(leg, Vector3(0.17, 0.60, 0.19), Vector3(0.0, -0.28, 0.0), trousers)
		_box(leg, Vector3(0.18, 0.10, 0.28), Vector3(0.0, -0.64, -0.04), shoes)
		var arm: Node3D = Node3D.new()
		arm.position = Vector3(side * 0.285, 1.31, 0.0)
		_visual.add_child(arm)
		_box(arm, Vector3(0.15, 0.22, 0.19), Vector3(0.0, -0.09, 0.0), shirt)
		_box(arm, Vector3(0.11, 0.37, 0.13), Vector3(0.0, -0.36, 0.0), skin)
		if side < 0.0:
			_left_leg = leg
			_left_arm = arm
		else:
			_right_leg = leg
			_right_arm = arm
	_visual.scale = Vector3.ONE * _rng.randf_range(0.93, 1.08)

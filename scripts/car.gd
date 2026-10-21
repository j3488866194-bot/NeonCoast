extends CharacterBody3D
## Arcade vehicle. All driver controls are injected by the game controller.

signal crashed(impact: float)

@export var paint: Color = Color("ed5c3e")
@export var is_police: bool = false
var controlled: bool = false
var driver_present: bool = false
var throttle: float = 0.0
var steering: float = 0.0
var braking: bool = false
var boost: bool = false
var lights_on: bool = true
var npc_target: Vector3 = Vector3.ZERO
var npc_driving: bool = false
var speed: float = 0.0
var health: float = 100.0
var model_name: String = "Comet GT"

var _planar_velocity: Vector3 = Vector3.ZERO
var _wheel_steer: Array[Node3D] = []
var _wheel_spin: Array[Node3D] = []
var _brake_material: StandardMaterial3D
var _headlamps: Array[SpotLight3D] = []
var _police_lamps: Array[MeshInstance3D] = []
var _police_materials: Array[StandardMaterial3D] = []
var _shell: Node3D
var _engine_glow: StandardMaterial3D
var _steer_angle: float = 0.0
var _crash_cooldown: float = 0.0
var _clock: float = 0.0
var _previous_forward_speed: float = 0.0

func _ready() -> void:
	collision_layer = 2
	collision_mask = 7
	floor_snap_length = 0.4
	floor_stop_on_slope = true
	floor_max_angle = deg_to_rad(42.0)
	var collision: CollisionShape3D = CollisionShape3D.new()
	var shape: BoxShape3D = BoxShape3D.new()
	shape.size = Vector3(1.92, 1.40, 4.35)
	collision.shape = shape
	collision.position = Vector3(0.0, 0.72, 0.0)
	add_child(collision)
	_build_car()
	if is_police:
		model_name = "Coast Patrol"
	add_to_group("vehicles")

func _physics_process(delta: float) -> void:
	_clock += delta
	_crash_cooldown = maxf(0.0, _crash_cooldown - delta)
	var drive: float = clampf(throttle, -1.0, 1.0)
	var turn: float = clampf(steering, -1.0, 1.0)
	var handbrake: bool = braking
	if npc_driving and not controlled:
		var to_target: Vector3 = npc_target - global_position
		to_target.y = 0.0
		if to_target.length() > 2.7:
			var local_target: Vector3 = global_transform.basis.inverse() * to_target.normalized()
			turn = clampf(local_target.x * 2.4, -1.0, 1.0)
			drive = clampf((11.5 - absf(speed)) * 0.35, -0.15, 0.75)
			if local_target.z > 0.25 and absf(speed) < 2.0:
				drive = 0.65
		else:
			drive = 0.0
			handbrake = true
	elif not controlled:
		drive = 0.0
		turn = 0.0
		handbrake = true
	if health <= 0.0:
		drive = 0.0
		handbrake = true
	var forward: Vector3 = -global_transform.basis.z
	forward.y = 0.0
	forward = forward.normalized()
	speed = _planar_velocity.dot(forward)
	_steer_angle = lerpf(_steer_angle, turn, minf(1.0, delta * 7.5))
	var speed_fraction: float = clampf(absf(speed) / 13.0, 0.0, 1.0)
	var turn_rate: float = lerpf(1.5, 0.83, clampf(absf(speed) / 44.0, 0.0, 1.0))
	if absf(speed) > 0.3:
		rotation.y -= _steer_angle * turn_rate * speed_fraction * signf(speed) * delta * (1.25 if handbrake else 1.0)
	forward = -global_transform.basis.z
	forward.y = 0.0
	forward = forward.normalized()
	var forward_speed: float = _planar_velocity.dot(forward)
	var lateral: Vector3 = _planar_velocity - forward * forward_speed
	lateral = lateral.move_toward(Vector3.ZERO, (3.8 if handbrake else 15.0) * delta)
	var max_speed: float = 49.0 if boost and controlled and drive > 0.1 else 38.0
	if health < 30.0:
		max_speed *= 0.67
	var acceleration: float = 13.5 * (1.5 if boost and controlled else 1.0)
	if drive < -0.05 and forward_speed > 0.8:
		forward_speed = move_toward(forward_speed, 0.0, 24.0 * absf(drive) * delta)
	elif drive > 0.05 and forward_speed < -0.8:
		forward_speed = move_toward(forward_speed, 0.0, 22.0 * drive * delta)
	elif absf(drive) > 0.05:
		forward_speed += drive * acceleration * delta * clampf(1.0 - maxf(0.0, forward_speed) / (max_speed * 1.45), 0.3, 1.0)
	else:
		forward_speed = move_toward(forward_speed, 0.0, (1.2 + absf(forward_speed) * 0.025) * delta)
	if handbrake:
		forward_speed = move_toward(forward_speed, 0.0, 18.0 * delta)
	forward_speed = clampf(forward_speed, -11.0, max_speed)
	_planar_velocity = forward * forward_speed + lateral
	var before_collision: Vector3 = _planar_velocity
	velocity.x = _planar_velocity.x
	velocity.z = _planar_velocity.z
	if not is_on_floor():
		velocity.y -= 24.0 * delta
	else:
		velocity.y = -0.5
	move_and_slide()
	_planar_velocity = Vector3(velocity.x, 0.0, velocity.z)
	for index: int in get_slide_collision_count():
		var contact: KinematicCollision3D = get_slide_collision(index)
		# One Jolt slide collision can contain both floor and wall contacts.
		for contact_index: int in contact.get_collision_count():
			var normal: Vector3 = contact.get_normal(contact_index)
			if absf(normal.y) < 0.65:
				var impact: float = maxf(0.0, -before_collision.dot(normal))
				if impact > 5.0 and _crash_cooldown <= 0.0:
					_crash_cooldown = 0.65
					take_damage((impact - 4.0) * 0.48)
					crashed.emit(impact)
					_planar_velocity += normal * minf(impact * 0.13, 2.8)
	var bounded: Vector3 = global_position
	bounded.x = clampf(bounded.x, -246.0, 246.0)
	bounded.z = clampf(bounded.z, -246.0, 246.0)
	if bounded != global_position:
		global_position = bounded
		_planar_velocity *= 0.25
	if global_position.y < -8.0:
		reset_at(Vector3(global_position.x, 2.0, global_position.z), rotation.y)
	speed = _planar_velocity.dot(forward)
	_update_visuals(delta, drive, handbrake)

func take_damage(amount: float) -> void:
	health = clampf(health - maxf(amount, 0.0), 0.0, 100.0)

func repair() -> void:
	health = 100.0

func reset_at(pos: Vector3, yaw: float = 0.0) -> void:
	global_position = pos
	rotation = Vector3(0.0, yaw, 0.0)
	velocity = Vector3.ZERO
	_planar_velocity = Vector3.ZERO
	speed = 0.0
	_crash_cooldown = 1.0

func _update_visuals(delta: float, drive: float, handbrake: bool) -> void:
	for wheel: Node3D in _wheel_spin:
		wheel.rotation.x -= speed * delta / 0.36
	for wheel: Node3D in _wheel_steer:
		wheel.rotation.y = -_steer_angle * 0.42
	var deceleration: float = (speed - _previous_forward_speed) / maxf(delta, 0.001)
	_previous_forward_speed = speed
	_shell.rotation.z = lerpf(_shell.rotation.z, -_steer_angle * clampf(speed / 30.0, -1.0, 1.0) * 0.035, delta * 6.0)
	_shell.rotation.x = lerpf(_shell.rotation.x, clampf(-deceleration * 0.0012, -0.035, 0.035), delta * 6.0)
	_brake_material.emission_energy_multiplier = 3.5 if handbrake or (drive < -0.05 and speed > 0.0) else 0.65
	_engine_glow.emission_energy_multiplier = 3.5 if boost and controlled and drive > 0.1 else 0.0
	for lamp: SpotLight3D in _headlamps:
		lamp.visible = lights_on and controlled
	for index: int in _police_materials.size():
		var lit: bool = (int(_clock * 7.0) + index) % 2 == 0
		_police_materials[index].emission_energy_multiplier = 5.0 if lit else 0.25

func _material(color: Color, metallic: float = 0.0, roughness: float = 0.55, glow: float = 0.0) -> StandardMaterial3D:
	var material: StandardMaterial3D = StandardMaterial3D.new()
	material.albedo_color = color
	material.metallic = metallic
	material.roughness = roughness
	if glow > 0.0:
		material.emission_enabled = true
		material.emission = color
		material.emission_energy_multiplier = glow
	return material

func _box(parent: Node3D, dimensions: Vector3, at: Vector3, material: Material) -> MeshInstance3D:
	var instance: MeshInstance3D = MeshInstance3D.new()
	var mesh: BoxMesh = BoxMesh.new()
	mesh.size = dimensions
	instance.mesh = mesh
	instance.material_override = material
	instance.position = at
	parent.add_child(instance)
	return instance

func _cylinder(parent: Node3D, radius: float, height: float, at: Vector3, material: Material, segments: int = 16) -> MeshInstance3D:
	var instance: MeshInstance3D = MeshInstance3D.new()
	var mesh: CylinderMesh = CylinderMesh.new()
	mesh.top_radius = radius
	mesh.bottom_radius = radius
	mesh.height = height
	mesh.radial_segments = segments
	instance.mesh = mesh
	instance.material_override = material
	instance.position = at
	parent.add_child(instance)
	return instance

func _poly(parent: Node3D, points: Array[Vector3], faces: Array, material: Material) -> MeshInstance3D:
	var surface: SurfaceTool = SurfaceTool.new()
	surface.begin(Mesh.PRIMITIVE_TRIANGLES)
	for face: Array in faces:
		for triangle: int in range(1, face.size() - 1):
			for vertex_index: int in [face[0], face[triangle], face[triangle + 1]]:
				surface.add_vertex(points[vertex_index])
	surface.generate_normals()
	var instance: MeshInstance3D = MeshInstance3D.new()
	instance.mesh = surface.commit()
	instance.material_override = material
	parent.add_child(instance)
	return instance

func _build_car() -> void:
	_shell = Node3D.new()
	_shell.name = "Coachwork"
	add_child(_shell)
	var color: Color = Color("eceddf") if is_police else paint
	var body: StandardMaterial3D = _material(color, 0.55, 0.25)
	body.cull_mode = BaseMaterial3D.CULL_DISABLED
	var black: StandardMaterial3D = _material(Color("121d27"), 0.35, 0.35)
	var rubber: StandardMaterial3D = _material(Color("11141a"), 0.0, 0.95)
	var alloy: StandardMaterial3D = _material(Color("a5b5bc"), 0.85, 0.25)
	var glass: StandardMaterial3D = _material(Color("183c4e"), 0.6, 0.12)
	glass.cull_mode = BaseMaterial3D.CULL_DISABLED
	var cabin: StandardMaterial3D = _material(Color("1c242d"), 0.0, 0.85)
	var light: StandardMaterial3D = _material(Color("e0faff"), 0.0, 0.3, 2.0)
	_brake_material = _material(Color("ff3348"), 0.0, 0.35, 0.65)
	_engine_glow = _material(Color("59cfff"), 0.0, 0.3, 0.1)
	_box(_shell, Vector3(1.84, 0.18, 4.12), Vector3(0.0, 0.34, 0.0), black)
	_poly(_shell, [Vector3(-0.88, 0.42, -2.2), Vector3(0.88, 0.42, -2.2), Vector3(0.96, 0.42, 2.1), Vector3(-0.96, 0.42, 2.1), Vector3(-0.89, 0.82, -2.16), Vector3(0.89, 0.82, -2.16), Vector3(0.97, 0.91, 2.02), Vector3(-0.97, 0.91, 2.02)], [[0,3,2,1], [0,1,5,4], [1,2,6,5], [2,3,7,6], [3,0,4,7], [4,5,6,7]], body)
	# Sloping bonnet and tapered cabin give the silhouette its low sports-car stance.
	_poly(_shell, [Vector3(-0.87, 0.82, -2.16), Vector3(0.87, 0.82, -2.16), Vector3(0.86, 1.00, -0.69), Vector3(-0.86, 1.00, -0.69)], [[0,1,2,3]], body)
	_poly(_shell, [Vector3(-0.85, 0.95, -0.74), Vector3(0.85, 0.95, -0.74), Vector3(0.86, 0.95, 1.46), Vector3(-0.86, 0.95, 1.46), Vector3(-0.68, 1.48, -0.13), Vector3(0.68, 1.48, -0.13), Vector3(0.7, 1.48, 0.76), Vector3(-0.7, 1.48, 0.76)], [[0,1,5,4], [1,2,6,5], [2,3,7,6], [3,0,4,7], [4,5,6,7]], black if is_police else body)
	_poly(_shell, [Vector3(-0.78, 1.01, -0.707), Vector3(0.78, 1.01, -0.707), Vector3(0.63, 1.444, -0.176), Vector3(-0.63, 1.444, -0.176)], [[0,1,2,3]], glass)
	_poly(_shell, [Vector3(-0.77, 1.01, 1.402), Vector3(0.77, 1.01, 1.402), Vector3(0.65, 1.436, 0.817), Vector3(-0.65, 1.436, 0.817)], [[0,1,2,3]], glass)
	for side: float in [-1.0, 1.0]:
		_poly(_shell, [Vector3(side * 0.854, 1.005, -0.622), Vector3(side * 0.854, 1.005, 0.28), Vector3(side * 0.704, 1.435, 0.28), Vector3(side * 0.692, 1.435, -0.1)], [[0,1,2,3]], glass)
		_poly(_shell, [Vector3(side * 0.861, 1.005, 0.36), Vector3(side * 0.861, 1.005, 1.32), Vector3(side * 0.712, 1.435, 0.73), Vector3(side * 0.707, 1.435, 0.36)], [[0,1,2,3]], glass)
		_box(_shell, Vector3(0.04, 0.035, 0.26), Vector3(side * 0.982, 0.88, 0.43), alloy)
		_box(_shell, Vector3(0.12, 0.08, 0.18), Vector3(side * 0.94, 1.07, -0.49), black)
		_box(_shell, Vector3(0.19, 0.12, 0.28), Vector3(side * 1.065, 1.1, -0.44), body)
		_box(_shell, Vector3(0.035, 0.13, 2.15), Vector3(side * 0.978, 0.53, 0.08), black)
		_box(_shell, Vector3(0.47, 0.46, 0.20), Vector3(side * 0.40, 1.01, 0.42), cabin)
		_box(_shell, Vector3(0.29, 0.22, 0.15), Vector3(side * 0.40, 1.34, 0.43), cabin)
		var headlight: MeshInstance3D = _box(_shell, Vector3(0.57, 0.105, 0.04), Vector3(side * 0.59, 0.755, -2.17), light)
		headlight.rotation.z = side * -0.045
		_box(_shell, Vector3(0.58, 0.10, 0.04), Vector3(side * 0.59, 0.805, 2.105), _brake_material)
		_box(_shell, Vector3(0.24, 0.09, 0.06), Vector3(side * 0.61, 0.38, 2.12), alloy)
		_box(_shell, Vector3(0.16, 0.055, 0.065), Vector3(side * 0.61, 0.38, 2.16), _engine_glow)
		var lamp: SpotLight3D = SpotLight3D.new()
		lamp.position = Vector3(side * 0.60, 0.80, -2.19)
		lamp.rotation.x = -0.10
		lamp.light_color = Color("dcf8ff")
		lamp.light_energy = 1.8
		lamp.spot_range = 23.0
		lamp.spot_angle = 29.0
		lamp.shadow_enabled = false
		_shell.add_child(lamp)
		_headlamps.append(lamp)
	_box(_shell, Vector3(1.32, 0.15, 0.07), Vector3(0.0, 0.48, -2.21), black)
	_box(_shell, Vector3(0.70, 0.12, 0.02), Vector3(0.0, 0.48, -2.251), rubber)
	for grille: int in range(-4, 5):
		_box(_shell, Vector3(0.025, 0.09, 0.015), Vector3(grille * 0.075, 0.48, -2.268), alloy)
	_box(_shell, Vector3(0.36, 0.12, 0.035), Vector3(0.0, 0.66, 2.13), _material(Color("f1e8cc")))
	_box(_shell, Vector3(0.17, 0.04, 0.02), Vector3(0.0, 0.87, 2.13), alloy)
	for side: float in [-1.0, 1.0]:
		for wheel_z: float in [-1.38, 1.38]:
			var steer_pivot: Node3D = Node3D.new()
			steer_pivot.position = Vector3(side * 0.98, 0.39, wheel_z)
			add_child(steer_pivot)
			if wheel_z < 0.0:
				_wheel_steer.append(steer_pivot)
			var spin_pivot: Node3D = Node3D.new()
			steer_pivot.add_child(spin_pivot)
			_wheel_spin.append(spin_pivot)
			var tire: MeshInstance3D = _cylinder(spin_pivot, 0.37, 0.25, Vector3.ZERO, rubber, 20)
			tire.rotation.z = PI / 2.0
			var rim: MeshInstance3D = _cylinder(spin_pivot, 0.264, 0.018, Vector3(side * 0.136, 0.0, 0.0), alloy)
			rim.rotation.z = PI / 2.0
			var recess: MeshInstance3D = _cylinder(spin_pivot, 0.211, 0.02, Vector3(side * 0.15, 0.0, 0.0), black)
			recess.rotation.z = PI / 2.0
			for spoke: int in 5:
				var angle: float = TAU * spoke / 5.0
				var bar: MeshInstance3D = _box(spin_pivot, Vector3(0.025, 0.21, 0.045), Vector3(side * 0.166, cos(angle) * 0.11, sin(angle) * 0.11), alloy)
				bar.rotation.x = angle
			var hub: MeshInstance3D = _cylinder(spin_pivot, 0.07, 0.03, Vector3(side * 0.181, 0.0, 0.0), alloy)
			hub.rotation.z = PI / 2.0
	if is_police:
		_box(_shell, Vector3(1.42, 0.07, 0.29), Vector3(0.0, 1.55, 0.24), black)
		for index: int in 2:
			var police_material: StandardMaterial3D = _material(Color("ff234a") if index == 0 else Color("3e7dff"), 0.1, 0.3, 3.0)
			_police_materials.append(police_material)
			_police_lamps.append(_box(_shell, Vector3(0.59, 0.14, 0.25), Vector3(-0.36 if index == 0 else 0.36, 1.64, 0.24), police_material))
		for side: float in [-1.0, 1.0]:
			_box(_shell, Vector3(0.018, 0.18, 1.45), Vector3(side * 0.981, 0.74, 0.15), black)
			var badge: MeshInstance3D = _cylinder(_shell, 0.115, 0.025, Vector3(side * 0.998, 0.76, 0.12), _material(Color("e8c77c"), 0.5), 6)
			badge.rotation.z = PI / 2.0
		_box(_shell, Vector3(0.035, 0.49, 0.035), Vector3(-0.57, 1.25, 1.85), black)
	else:
		# Floating rear wing and a pair of charcoal hood insets.
		_box(_shell, Vector3(1.73, 0.06, 0.27), Vector3(0.0, 1.12, 1.85), black)
		for side: float in [-1.0, 1.0]:
			_box(_shell, Vector3(0.055, 0.18, 0.14), Vector3(side * 0.64, 1.01, 1.85), black)
			var vent: MeshInstance3D = _box(_shell, Vector3(0.20, 0.014, 0.58), Vector3(side * 0.56, 0.92, -1.38), black)
			vent.rotation.x = -0.122

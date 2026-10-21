extends Node3D
## Neon Coast: application flow, simulation, mission orchestration and camera.
const City = preload("res://scripts/city.gd")
const Car = preload("res://scripts/car.gd")
const Player = preload("res://scripts/player.gd")
const Pedestrian = preload("res://scripts/pedestrian.gd")
const HUD = preload("res://scripts/hud.gd")
const Missions = preload("res://scripts/missions.gd")
const SaveStore = preload("res://scripts/save_store.gd")
const InputSetup = preload("res://scripts/input_setup.gd")
const Sound = preload("res://scripts/audio.gd")
const START: Vector3 = Vector3(216,0.3,30)
const REPAIR: Vector3 = Vector3(216,0,0)
const CARD_SPOTS: Array[Vector3] = [Vector3(228,0,38),Vector3(228,0,-118),Vector3(228,0,178),Vector3(132,0,-72),Vector3(60,0,-144),Vector3(-60,0,-216),Vector3(-156,0,72),Vector3(-216,0,132),Vector3(0,0,156),Vector3(156,0,216)]

var world: Node3D
var city: Node3D
var player: CharacterBody3D
var hero_car: CharacterBody3D
var current_car: CharacterBody3D
var hud: CanvasLayer
var missions: Node
var sound: Node
var camera: Camera3D
var cars: Array[CharacterBody3D] = []
var pedestrians: Array[Node3D] = []
var patrols: Array[CharacterBody3D] = []
var traffic_routes: Dictionary = {}
var cards: Array[Node3D] = []
var collected: Array = []
var mode: String = "title"
var settings_return: String = "title"
var cash: int = 250
var heat: float = 0.0
var wanted: int = 0
var unseen_time: float = 0.0
var time_of_day: float = 17.6
var total_time: float = 0.0
var active_time: float = 0.0
var save_clock: float = 0.0
var shot_cooldown: float = 0.0
var damage_cooldown: float = 0.0
var camera_yaw: float = 0.0
var camera_pitch: float = -0.19
var camera_style: int = 1
var look_idle: float = 0.0
var using_controller: bool = false
var active_pad: int = -1
var radio_station: int = 0
var waypoint: Vector3 = Vector3.ZERO
var waypoint_active: bool = false
var has_started: bool = false
var has_save: bool = false
var profile: Dictionary = {}
var settings: Dictionary = {"volume":0.5,"sensitivity":1.0,"quality":"high","invert_y":false}
var objective_ring: Node3D
var waypoint_ring: Node3D
var garage_ring: Node3D
var near_vehicle: CharacterBody3D
var status: Dictionary = {}
var rng: RandomNumberGenerator = RandomNumberGenerator.new()
var _last_city_update: float = 0.0
var _screenshot_mode: bool = false
var _test_mode: bool = false
var _capture_request: String = ""
var _last_capture_time: float = -10.0

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	rng.seed = 20261021
	InputSetup.configure()
	profile = SaveStore.load_save()
	has_save = not profile.is_empty()
	settings.merge(SaveStore.load_settings(),true)
	world = Node3D.new()
	world.name = "Simulation"
	world.process_mode = Node.PROCESS_MODE_PAUSABLE
	add_child(world)
	city = City.new()
	world.add_child(city)
	player = Player.new()
	player.position = START + Vector3(3.1,0.0,1.5)
	world.add_child(player)
	hero_car = _spawn_car(START, Color("f07850"), false)
	_spawn_traffic()
	_spawn_people()
	objective_ring = _marker(Color("ffd88b"),4.6,true)
	waypoint_ring = _marker(Color("70e1ce"),3.3,false)
	waypoint_ring.visible = false
	garage_ring = _marker(Color("70e1ce"),3.8,false)
	garage_ring.position = REPAIR
	_spawn_collectibles()
	missions = Missions.new()
	world.add_child(missions)
	missions.completed.connect(_mission_completed)
	missions.message.connect(_notify)
	missions.heat_requested.connect(_raise_heat)
	camera = Camera3D.new()
	camera.far = 1500.0
	camera.near = 0.12
	camera.fov = 65.0
	add_child(camera)
	camera.make_current()
	sound = Sound.new()
	world.add_child(sound)
	sound.set_master_volume(float(settings.volume))
	hud = HUD.new()
	add_child(hud)
	hud.set_map_blocks(city.get_map_blocks())
	hud.action_requested.connect(_ui_action)
	hud.setting_changed.connect(_setting_changed)
	hud.waypoint_selected.connect(_set_waypoint)
	Input.joy_connection_changed.connect(_joy_connection)
	var devices: Array[int] = Input.get_connected_joypads()
	if not devices.is_empty(): active_pad = devices[0]
	_apply_quality()
	get_tree().auto_accept_quit = false
	var args: PackedStringArray = OS.get_cmdline_user_args()
	_test_mode = "--test-mode" in args
	if "--autoplay" in args:
		_start_game(false)
	else:
		_set_mode("title")
	if "--capture-title" in args:
		_capture_request = "title"
	elif "--capture-game" in args:
		_start_game(false)
		_enter_car(hero_car)
		_capture_request = "game"
	print("NEON_COAST_READY | mode=",mode," | cars=",cars.size()," | pedestrians=",pedestrians.size())

func _spawn_car(at: Vector3, paint: Color, police: bool) -> CharacterBody3D:
	var car: CharacterBody3D = Car.new()
	car.paint = paint
	car.is_police = police
	car.position = at
	world.add_child(car)
	cars.append(car)
	car.crashed.connect(_car_crashed.bind(car))
	return car

func _spawn_traffic() -> void:
	var palettes: Array[Color] = [Color("c8ded7"),Color("e2c77a"),Color("3e7589"),Color("bb505d"),Color("e7e5d5"),Color("36394b")]
	for i: int in 12:
		var x0: float = -216.0 + float(i % 3) * 144.0
		var z0: float = -216.0 + float((i / 3) % 3) * 144.0
		var route: Array[Vector3] = [Vector3(x0+4,0.2,z0+4),Vector3(x0+4,0.2,z0+68),Vector3(x0+68,0.2,z0+68),Vector3(x0+68,0.2,z0+4)]
		var slot: int = i % 4
		var car: CharacterBody3D = _spawn_car(route[slot],palettes[i%palettes.size()],false)
		var next: int = (slot+1)%4
		var d: Vector3 = route[next]-route[slot]
		car.rotation.y = atan2(-d.x,-d.z)
		car.npc_target = route[next]
		car.npc_driving = true
		car.driver_present = true
		traffic_routes[car.get_instance_id()] = {"route":route,"next":next,"stuck":0.0}
	# A few parked cars are available to steal without interrupting every traffic lane.
	for i: int in 4:
		var car: CharacterBody3D = _spawn_car(Vector3(210,0.2,-156+i*72),palettes[(i+2)%6],false)
		car.rotation.y = 0.0

func _spawn_people() -> void:
	for i: int in 32:
		var person: Node3D = Pedestrian.new()
		var x: float = 228.0 if i < 14 else -206.0 + float(i%6)*72.0
		var z: float = -196.0 + float(i%14)*29.0
		person.origin = Vector3(x,0.16,z)
		person.seed_value = i*197+13
		person.position = person.origin
		world.add_child(person)
		person.set_walk_axis(Vector3.FORWARD,5.5)
		pedestrians.append(person)

func _spawn_collectibles() -> void:
	for i: int in CARD_SPOTS.size():
		var card: Node3D = Node3D.new()
		card.position = CARD_SPOTS[i] + Vector3(0,1.3,0)
		world.add_child(card)
		var mesh: MeshInstance3D = MeshInstance3D.new()
		var box: BoxMesh = BoxMesh.new()
		box.size = Vector3(0.6,0.8,0.08)
		mesh.mesh = box
		var mat: StandardMaterial3D = StandardMaterial3D.new()
		mat.albedo_color = Color("7be7d2")
		mat.emission_enabled = true
		mat.emission = mat.albedo_color
		mat.emission_energy_multiplier = 0.7
		mesh.material_override = mat
		card.add_child(mesh)
		cards.append(card)

func _marker(color: Color, radius: float, beam: bool) -> Node3D:
	var marker: Node3D = Node3D.new()
	world.add_child(marker)
	var mat: StandardMaterial3D = StandardMaterial3D.new()
	mat.albedo_color = color
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.emission_enabled = true
	mat.emission = color
	mat.emission_energy_multiplier = 1.3
	var ring: MeshInstance3D = MeshInstance3D.new()
	var torus: TorusMesh = TorusMesh.new()
	torus.inner_radius = radius-0.14
	torus.outer_radius = radius
	torus.rings = 32
	torus.ring_segments = 8
	ring.mesh = torus
	ring.material_override = mat
	ring.position.y = 0.15
	ring.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	marker.add_child(ring)
	if beam:
		var arrow: MeshInstance3D = MeshInstance3D.new()
		var cone: CylinderMesh = CylinderMesh.new()
		cone.top_radius = 0.75
		cone.bottom_radius = 0.0
		cone.height = 1.25
		cone.radial_segments = 4
		arrow.mesh = cone
		arrow.position.y = 5.4
		arrow.material_override = mat
		marker.add_child(arrow)
	return marker

func _process(delta: float) -> void:
	total_time += delta
	look_idle += delta
	if mode == "playing":
		_update_look(delta)
		_update_camera(delta)
	elif mode == "title":
		var angle: float = sin(total_time*0.028)*0.22
		camera.position = Vector3(304+sin(angle)*100,94,220+cos(angle)*50)
		camera.look_at(Vector3(40,18,5),Vector3.UP)
	elif mode == "photo":
		_update_photo(delta)
	_update_hud()
	if not _capture_request.is_empty() and total_time > 7.0:
		var kind: String = _capture_request
		_capture_request = ""
		_capture(kind,true)
	if _capture_request.is_empty() and "--capture-exit" in OS.get_cmdline_user_args() and total_time > 9.0:
		get_tree().quit()

func _physics_process(delta: float) -> void:
	if mode != "playing": return
	active_time += delta
	save_clock += delta
	shot_cooldown = maxf(0.0,shot_cooldown-delta)
	damage_cooldown = maxf(0.0,damage_cooldown-delta)
	var move: Vector2 = Input.get_vector("move_left","move_right","move_forward","move_back")
	if is_instance_valid(current_car):
		current_car.throttle = Input.get_action_strength("throttle")-Input.get_action_strength("reverse")
		current_car.steering = move.x
		current_car.braking = Input.is_action_pressed("brake")
		current_car.boost = Input.is_action_pressed("boost")
		player.global_position = current_car.global_position
		if current_car.health <= 0.0:
			_exit_car()
			_notify("车辆损坏", "换一辆车，或按 T / 十字键下救援。")
	else:
		player.move_input = move
		player.view_yaw = camera_yaw
		player.sprinting = Input.is_action_pressed("sprint")
		player.jump_requested = Input.is_action_just_pressed("jump")
		player.aiming = Input.is_action_pressed("aim")
		if Input.is_action_pressed("fire") and player.aiming and shot_cooldown <= 0.0:
			_fire()
	near_vehicle = _nearest_car()
	_update_traffic(delta)
	_update_police(delta)
	var at: Vector3 = _actor_position()
	missions.tick(delta,at,is_instance_valid(current_car),wanted,current_car.speed if is_instance_valid(current_car) else 0.0)
	status = missions.status()
	objective_ring.visible = bool(status.has_target)
	objective_ring.position = status.target
	if objective_ring.get_child_count()>1:
		objective_ring.get_child(1).position.y = 5.3+sin(active_time*2.2)*0.35
		objective_ring.get_child(1).rotation.y += delta
	waypoint_ring.visible = waypoint_active
	waypoint_ring.position = waypoint
	if waypoint_active and at.distance_to(waypoint)<7.0:
		waypoint_active = false
		_notify("已到达标记位置", "打开地图可以设置新的导航点。")
	_update_collectibles(delta,at)
	for person: Node3D in pedestrians:
		if is_instance_valid(current_car) and absf(current_car.speed)>7.0 and person.global_position.distance_to(at)<5.0:
			person.start_panic(at)
			if person.global_position.distance_to(at)<1.7 and damage_cooldown<=0.0:
				person.take_damage(45.0)
				_raise_heat(18)
				damage_cooldown = 0.8
	if player.health<=0.0:
		_respawn()
	time_of_day = fmod(time_of_day+delta/200.0,24.0)
	if active_time-_last_city_update>2.0:
		city.set_time_of_day(time_of_day)
		_last_city_update = active_time
	sound.speed = absf(current_car.speed) if is_instance_valid(current_car) else 0.0
	sound.wanted = wanted
	sound.in_vehicle = is_instance_valid(current_car)
	if save_clock>25.0 and wanted==0:
		_save()

func _input(event: InputEvent) -> void:
	if event is InputEventJoypadButton or event is InputEventJoypadMotion:
		if event is InputEventJoypadButton or absf(event.axis_value)>0.25:
			using_controller = true
			active_pad = event.device
	elif event is InputEventKey or event is InputEventMouseButton:
		using_controller = false
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED and mode in ["playing","photo"]:
		var sensitivity: float = float(settings.sensitivity)*0.0027
		camera_yaw -= event.relative.x*sensitivity
		camera_pitch = clampf(camera_pitch-event.relative.y*sensitivity*(-1.0 if bool(settings.invert_y) else 1.0),-1.0,0.48)
		look_idle = 0.0
	if event.is_action_pressed("pause"):
		if mode == "playing": _set_mode("paused")
		elif mode in ["paused","map","photo"]: _set_mode("playing")
		elif mode == "settings": _set_mode(settings_return)
		get_viewport().set_input_as_handled()
		return
	if event.is_action_pressed("map") and mode in ["playing","map"]:
		_set_mode("playing" if mode=="map" else "map")
		get_viewport().set_input_as_handled()
		return
	if event.is_action_pressed("photo") and mode in ["playing","photo"]:
		_set_mode("playing" if mode=="photo" else "photo")
		return
	if event is InputEventKey and event.pressed and event.keycode==KEY_F12:
		_capture("photo",false)
	if mode=="photo" and event is InputEventJoypadButton and event.is_action_pressed("reload"):
		_capture("photo",false)
	if mode != "playing": return
	if event.is_action_pressed("interact"):
		_interact()
	elif event.is_action_pressed("camera"):
		camera_style = (camera_style+1)%3
	elif event.is_action_pressed("radio") and is_instance_valid(current_car):
		radio_station = (radio_station+1)%3
		sound.set_radio(radio_station)
		_notify(_radio_name(), "原创合成器电台" if radio_station!=2 else "享受海风和引擎声。")
	elif event.is_action_pressed("horn") and is_instance_valid(current_car):
		sound.play_effect("horn")
		for person: Node3D in pedestrians:
			if person.position.distance_to(current_car.position)<18.0: person.start_panic(current_car.position)
	elif event.is_action_pressed("headlights") and is_instance_valid(current_car):
		current_car.lights_on = not current_car.lights_on
	elif event.is_action_pressed("recover"):
		_recover()

func _update_look(delta: float) -> void:
	if active_pad<0: return
	var stick: Vector2 = Vector2(Input.get_joy_axis(active_pad,JOY_AXIS_RIGHT_X),Input.get_joy_axis(active_pad,JOY_AXIS_RIGHT_Y))
	if stick.length()>0.18:
		stick = stick.normalized()*((stick.length()-0.18)/0.82)
		camera_yaw -= stick.x*delta*2.6*float(settings.sensitivity)
		camera_pitch = clampf(camera_pitch-stick.y*delta*1.8*float(settings.sensitivity)*(-1.0 if bool(settings.invert_y) else 1.0),-1.0,0.48)
		look_idle = 0.0

func _update_camera(delta: float) -> void:
	var car_mode: bool = is_instance_valid(current_car)
	var at: Vector3 = _actor_position()+Vector3(0,1.45,0)
	var distance: float = [6.0,9.5,14.0][camera_style] if car_mode else 5.6
	var height: float = 1.7 if car_mode else 0.4
	if not car_mode and player.aiming:
		distance = 2.9
		at += Basis(Vector3.UP,camera_yaw)*Vector3(0.7,0.0,0.0)
	if car_mode and look_idle>1.6 and absf(current_car.speed)>3.0:
		camera_yaw = lerp_angle(camera_yaw,current_car.rotation.y,minf(delta*1.7,1.0))
	var orbit: Vector3 = Vector3(sin(camera_yaw)*cos(camera_pitch),-sin(camera_pitch),cos(camera_yaw)*cos(camera_pitch))
	var desired: Vector3 = at+orbit*distance+Vector3.UP*height
	var query: PhysicsRayQueryParameters3D = PhysicsRayQueryParameters3D.create(at,desired,1)
	var hit: Dictionary = world.get_world_3d().direct_space_state.intersect_ray(query)
	if not hit.is_empty(): desired = hit.position+(at-hit.position).normalized()*0.5
	camera.position = camera.position.lerp(desired,1.0-exp(-delta*8.0))
	if camera.position.distance_to(at)>0.1: camera.look_at(at,Vector3.UP)
	camera.fov = lerpf(camera.fov,clampf(65.0+absf(current_car.speed)*0.36,65,82) if car_mode else (51.0 if player.aiming else 64.0),minf(1.0,delta*4.0))

func _update_photo(delta: float) -> void:
	_update_look(delta)
	camera.rotation = Vector3(camera_pitch,camera_yaw,0)
	var motion: Vector2 = Input.get_vector("move_left","move_right","move_forward","move_back")
	var direction: Vector3 = camera.basis*Vector3(motion.x,0,motion.y)
	if Input.is_action_pressed("jump"): direction.y += 1.0
	if Input.is_key_pressed(KEY_CTRL) or Input.is_joy_button_pressed(active_pad,JOY_BUTTON_B): direction.y -= 1.0
	camera.position += direction*delta*(26.0 if Input.is_action_pressed("sprint") else 9.0)
	camera.position.y = clampf(camera.position.y,1.0,180.0)

func _update_hud() -> void:
	if not is_instance_valid(hud) or not is_instance_valid(missions): return
	status = missions.status()
	var at: Vector3 = _actor_position()
	var car_mode: bool = is_instance_valid(current_car)
	var target: Vector3 = waypoint if waypoint_active else status.target
	var has_target: bool = waypoint_active or bool(status.has_target)
	var prompt: String = str(status.prompt)
	if prompt.is_empty():
		if car_mode:
			prompt = "修理车辆 · $100" if at.distance_to(REPAIR)<9.0 and absf(current_car.speed)<2.5 else "下车"
		elif is_instance_valid(near_vehicle): prompt = "进入 " + near_vehicle.model_name
	var controls: String = "[Y]  " if using_controller else "[F]  "
	if not prompt.is_empty(): prompt = controls+prompt
	var hh: int = int(time_of_day)
	var mm: int = int(fmod(time_of_day,1.0)*60.0)
	hud.update_state({"player_position":at,"player_yaw":current_car.rotation.y if car_mode else camera_yaw,
		"vehicle":car_mode,"speed":absf(current_car.speed)*3.6 if car_mode else 0.0,
		"health":player.health,"car_health":current_car.health if car_mode else 100.0,"cash":cash,
		"wanted":wanted,"heat":heat,"mission_title":status.title,"objective":status.objective,
		"objective_position":target,"has_objective":has_target,"distance":Vector2(at.x-target.x,at.z-target.z).length(),
		"district":city.get_district(at),"time":"%02d:%02d" % [hh,mm],"controller":using_controller,
		"prompt":prompt,"radio":_radio_name(),"fps":Engine.get_frames_per_second(),"mission_index":missions.mission_index,
		"progress":status.progress,"save_exists":has_save,"settings":settings,"collectibles":collected.size(),
		"collectibles_total":CARD_SPOTS.size(),"hidden":unseen_time>2.0,"aiming":player.aiming and not car_mode,
		"waypoint_position":waypoint,"waypoint_active":waypoint_active})

func _update_traffic(delta: float) -> void:
	for car: CharacterBody3D in cars:
		var id: int = car.get_instance_id()
		if not traffic_routes.has(id) or car==current_car: continue
		var route_state: Dictionary = traffic_routes[id]
		var route: Array = route_state.route
		var index: int = int(route_state.next)
		if car.global_position.distance_to(route[index])<7.0:
			index = (index+1)%route.size()
			route_state.next = index
		car.npc_target = route[index]
		if absf(car.speed)<0.7: route_state.stuck = float(route_state.stuck)+delta
		else: route_state.stuck = 0.0
		if float(route_state.stuck)>12.0 and car.position.distance_to(_actor_position())>70:
			var to: Vector3 = route[index]
			var from: Vector3 = route[(index+3)%4]
			car.reset_at(from,atan2(-(to-from).x,-(to-from).z))
			route_state.stuck = 0.0

func _raise_heat(amount: float) -> void:
	heat = clampf(heat+amount,0,120)
	wanted = ceili(heat/24.0)
	unseen_time = 0.0

func _update_police(delta: float) -> void:
	var at: Vector3 = _actor_position()
	var seen: bool = false
	while patrols.size()<mini(wanted,4):
		var angle: float = rng.randf()*TAU
		var point: Vector3 = at+Vector3(sin(angle),0,cos(angle))*110.0
		point = _road_point(point)
		if point.distance_to(at)<45.0: point = _road_point(at+Vector3(-100,0,-100))
		var car: CharacterBody3D = _spawn_car(point+Vector3.UP*0.2,Color.WHITE,true)
		car.npc_driving = true
		car.driver_present = true
		var target: Vector3 = at-point
		car.rotation.y = atan2(-target.x,-target.z)
		patrols.append(car)
	for patrol: CharacterBody3D in patrols:
		if wanted==0:
			patrol.npc_driving = false
			continue
		var d: float = patrol.global_position.distance_to(at)
		patrol.npc_driving = patrol.health>0.0
		var road_target: Vector3 = _road_point(at)
		var p: Vector3 = patrol.position
		if d<25:
			patrol.npc_target = at
		elif absf(p.x-road_target.x)>14:
			var zroad: float = snappedf(p.z,72.0)
			patrol.npc_target = Vector3(p.x,0,zroad) if absf(p.z-zroad)>7.0 else Vector3(road_target.x,0,zroad)
		else:
			patrol.npc_target = road_target
		if d<72 and patrol.health>0:
			var query: PhysicsRayQueryParameters3D = PhysicsRayQueryParameters3D.create(patrol.position+Vector3.UP*1.8,at+Vector3.UP*1.4,1)
			if world.get_world_3d().direct_space_state.intersect_ray(query).is_empty(): seen = true
		if d<10.0 and patrol.health>0:
			player.take_damage(delta*(2.5 if is_instance_valid(current_car) else 7.0))
			if is_instance_valid(current_car): current_car.take_damage(delta*2.0)
	if wanted>0:
		if seen: unseen_time = 0.0
		else: unseen_time += delta
		if unseen_time>4.0: heat = maxf(0.0,heat-delta*4.0)
		wanted = ceili(heat/24.0)
		if wanted==0:
			_notify("通缉解除", "已脱离警方视线。城市重新属于你。")
			_save()
	else:
		for index: int in range(patrols.size()-1,-1,-1):
			var patrol: CharacterBody3D = patrols[index]
			if patrol.position.distance_to(at)>50.0:
				cars.erase(patrol)
				patrols.remove_at(index)
				patrol.queue_free()

func _road_point(p: Vector3) -> Vector3:
	var point: Vector3 = Vector3(clampf(p.x,-216,216),0.2,clampf(p.z,-216,216))
	if absf(point.x-snappedf(point.x,72))<absf(point.z-snappedf(point.z,72)):
		point.x = snappedf(point.x,72)
	else:
		point.z = snappedf(point.z,72)
	return point

func _interact() -> void:
	var at: Vector3 = _actor_position()
	if is_instance_valid(current_car) and absf(current_car.speed)>5.0:
		_notify("请先减速", "停车后可上下车或交互。")
		return
	if missions.interact(at,is_instance_valid(current_car)): return
	if is_instance_valid(current_car):
		if at.distance_to(REPAIR)<9.0 and current_car.health<99.0:
			if cash>=100:
				cash -= 100
				current_car.repair()
				player.health = 100
				_notify("修理完成", "车辆和生命值已恢复。")
			else: _notify("现金不足", "修理费用为 $100。")
		else: _exit_car()
	elif is_instance_valid(near_vehicle):
		_enter_car(near_vehicle)

func _nearest_car() -> CharacterBody3D:
	if is_instance_valid(current_car): return null
	var nearest: CharacterBody3D = null
	var distance: float = 4.5
	for car: CharacterBody3D in cars:
		var d: float = player.position.distance_to(car.position)
		if d<distance and absf(car.speed)<5.0 and car.health>0.0:
			distance = d
			nearest = car
	return nearest

func _enter_car(car: CharacterBody3D) -> void:
	if car.driver_present and car!=hero_car:
		_raise_heat(24 if car.is_police else 12)
		_notify("车辆已接管", "目击者已报警。")
	traffic_routes.erase(car.get_instance_id())
	# A hijacked patrol is the player's vehicle, never an active pursuer.
	patrols.erase(car)
	car.controlled = true
	car.driver_present = true
	car.npc_driving = false
	current_car = car
	player.set_seated(true)
	camera_yaw = car.rotation.y
	look_idle = 3.0
	sound.play_effect("confirm")

func _exit_car() -> void:
	if not is_instance_valid(current_car): return
	var car: CharacterBody3D = current_car
	var exit_at: Vector3 = _safe_exit_position(car)
	car.controlled = false
	car.throttle = 0
	car.braking = true
	car.driver_present = false
	current_car = null
	player.set_seated(false)
	player.position = exit_at
	player.aiming = false
	near_vehicle = car

func _safe_exit_position(car: CharacterBody3D) -> Vector3:
	var candidates: Array[Vector3] = []
	for offset: Vector3 in [car.basis.x*2.6,-car.basis.x*2.6,car.basis.z*3.4,-car.basis.z*3.4]:
		candidates.append(car.position+offset)
	return _safe_foot_position(car.position,candidates)

func _safe_foot_position(at: Vector3, candidates: Array[Vector3] = []) -> Vector3:
	if candidates.is_empty(): candidates.append(at)
	var road: Vector3 = _road_point(at)
	for offset: Vector3 in [Vector3.ZERO,Vector3(3,0,0),Vector3(-3,0,0),Vector3(0,0,5),Vector3(0,0,-5)]:
		candidates.append(road+offset)
	candidates.append(START+Vector3(3.1,0,1.5))
	var shape: CapsuleShape3D = CapsuleShape3D.new()
	shape.radius = 0.34
	shape.height = 1.82
	for point: Vector3 in candidates:
		point.x = clampf(point.x,-246,308)
		point.z = clampf(point.z,-246,246)
		var floor_query: PhysicsRayQueryParameters3D = PhysicsRayQueryParameters3D.create(point+Vector3.UP*2.5,point-Vector3.UP*5.0,1)
		var floor_hit: Dictionary = world.get_world_3d().direct_space_state.intersect_ray(floor_query)
		if floor_hit.is_empty() or floor_hit.normal.y<0.65: continue
		point = floor_hit.position+Vector3.UP*0.06
		var query: PhysicsShapeQueryParameters3D = PhysicsShapeQueryParameters3D.new()
		query.shape = shape
		query.transform = Transform3D(Basis.IDENTITY,point+Vector3.UP*0.93)
		query.collision_mask = 3
		if world.get_world_3d().direct_space_state.intersect_shape(query,1).is_empty(): return point
	return START+Vector3(3.1,0,1.5)

func _fire() -> void:
	shot_cooldown = 0.24
	sound.play_effect("shot")
	_raise_heat(3.0)
	var from: Vector3 = camera.global_position
	var direction: Vector3 = -camera.global_basis.z
	var end: Vector3 = from+direction*140.0
	var query: PhysicsRayQueryParameters3D = PhysicsRayQueryParameters3D.create(from,end,3)
	var hit: Dictionary = world.get_world_3d().direct_space_state.intersect_ray(query)
	if not hit.is_empty():
		end = hit.position
		if hit.collider.has_method("take_damage"):
			hit.collider.take_damage(28.0)
			if hit.collider.is_police: _raise_heat(14.0)
	for person: Node3D in pedestrians:
		var center: Vector3 = person.position+Vector3.UP
		var along: float = (center-from).dot(direction)
		if along>0.0 and along<from.distance_to(end) and center.distance_to(from+direction*along)<0.7:
			person.take_damage(40)
			_raise_heat(12)
		if person.position.distance_to(player.position)<28: person.start_panic(player.position)
	var tracer: MeshInstance3D = MeshInstance3D.new()
	var line: ImmediateMesh = ImmediateMesh.new()
	var mat: StandardMaterial3D = StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.albedo_color = Color("ffe8a6")
	line.surface_begin(Mesh.PRIMITIVE_LINES,mat)
	line.surface_add_vertex(player.position+Vector3.UP*1.35+player.global_basis.x*0.25)
	line.surface_add_vertex(end)
	line.surface_end()
	tracer.mesh = line
	world.add_child(tracer)
	get_tree().create_timer(0.055).timeout.connect(tracer.queue_free)
	if active_pad>=0: Input.start_joy_vibration(active_pad,0.1,0.35,0.08)

func _car_crashed(impact: float, car: CharacterBody3D) -> void:
	if car!=current_car: return
	sound.play_effect("crash")
	player.take_damage(maxf(0,impact-15)*0.18)
	if active_pad>=0: Input.start_joy_vibration(active_pad,clampf(impact/40,0,0.7),0.5,0.18)

func _update_collectibles(delta: float, at: Vector3) -> void:
	for i: int in cards.size():
		var card: Node3D = cards[i]
		card.visible = not collected.has(i)
		if not card.visible: continue
		card.rotation.y += delta*0.9
		card.position.y = 1.4+sin(active_time*1.8+i)*0.18
		if card.position.distance_to(at+Vector3.UP)<2.4:
			collected.append(i)
			cash += 150
			_notify("海岸明信片 %d / %d" % [collected.size(),cards.size()], "+$150 · 城市的每个角落，都有故事。")
			sound.play_effect("confirm")
			_save()

func _mission_completed(title: String, reward: int) -> void:
	cash += reward
	hud.show_mission_complete(title,reward)
	sound.play_effect("mission")
	_save()

func _start_game(resume: bool) -> void:
	heat = 0
	wanted = 0
	unseen_time = 0
	for patrol: CharacterBody3D in patrols:
		cars.erase(patrol)
		patrol.queue_free()
	patrols.clear()
	if is_instance_valid(current_car): _exit_car()
	hero_car.reset_at(START,0)
	hero_car.repair()
	player.reset_at(START+Vector3(3.1,0,1.5))
	if resume and has_save:
		profile = SaveStore.load_save()
		cash = int(profile.get("cash",250))
		missions.restore(int(profile.get("mission_index",0)),int(profile.get("mission_stage",0)))
		player.position = _decode_position(profile.get("position",[]),START+Vector3(3.1,0,1.5))+Vector3.UP*0.2
		hero_car.reset_at(_decode_position(profile.get("car_position",[]),START),float(profile.get("car_yaw",0)))
		player.position = _safe_foot_position(player.position)
		player.health = maxf(25,float(profile.get("health",100)))
		time_of_day = float(profile.get("time_of_day",17.6))
		collected = profile.get("collectibles",[]).duplicate()
	else:
		cash = 250
		missions.restore(0,0)
		collected.clear()
		time_of_day = 17.6
	city.set_time_of_day(time_of_day)
	has_started = true
	waypoint_active = false
	_set_mode("playing")
	camera_yaw = 0
	camera_pitch = -0.19
	camera.position = player.position+Vector3(0,4,8)
	_notify("欢迎来到霓虹海岸", "F / Y 上车 · M / View 地图 · Esc / Menu 暂停与操作说明")
	_save()

func _set_mode(next: String) -> void:
	mode = next
	if next != "playing":
		player.move_input = Vector2.ZERO
		player.sprinting = false
		player.aiming = false
		player.jump_requested = false
		if is_instance_valid(current_car):
			current_car.throttle = 0.0
			current_car.steering = 0.0
			current_car.braking = true
	get_tree().paused = next in ["paused","map","settings","photo"]
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if next in ["playing","photo"] else Input.MOUSE_MODE_VISIBLE
	hud.set_mode(next)
	if next=="photo":
		camera_pitch = camera.rotation.x
		camera_yaw = camera.rotation.y
	if next=="paused" and has_started: _save()

func _ui_action(action: String) -> void:
	match action:
		"start", "restart": _start_game(false)
		"continue": _start_game(true)
		"resume", "map_close", "close_map", "photo_exit": _set_mode("playing")
		"settings":
			settings_return = mode
			_set_mode("settings")
		"back", "settings_back": _set_mode(settings_return)
		"title", "main_menu":
			_save()
			_set_mode("title")
		"map": _set_mode("map")
		"photo": _set_mode("photo")
		"screenshot", "capture": _capture("photo",false)
		"clear_waypoint": waypoint_active = false
		"toggle_fullscreen": DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED if DisplayServer.window_get_mode()==DisplayServer.WINDOW_MODE_FULLSCREEN else DisplayServer.WINDOW_MODE_FULLSCREEN)
		"quit":
			_save()
			get_tree().quit()
		"reset_save":
			SaveStore.clear_save()
			has_save = false
			profile.clear()

func _setting_changed(key: String, value: float) -> void:
	match key:
		"volume":
			settings.volume = clampf(value,0,1)
			sound.set_master_volume(float(settings.volume))
		"sensitivity": settings.sensitivity = clampf(value,0.3,2.0)
		"invert_y": settings.invert_y = value>0.5
		"quality":
			settings.quality = ["low","medium","high"][clampi(int(value),0,2)]
			_apply_quality()
	if not _test_mode: SaveStore.write_settings(settings)

func _apply_quality() -> void:
	var quality: String = str(settings.quality)
	get_viewport().msaa_3d = Viewport.MSAA_DISABLED if quality=="low" else (Viewport.MSAA_2X if quality=="medium" else Viewport.MSAA_4X)
	for node: Node in city.get_children():
		if node is DirectionalLight3D: node.shadow_enabled = quality!="low"

func _set_waypoint(point: Vector3) -> void:
	waypoint = _road_point(point)
	waypoint.y = 0
	waypoint_active = true
	_notify("导航点已设置", "青色标记显示你的自定义目的地。")

func _save() -> void:
	if not has_started or _test_mode: return
	var parked: CharacterBody3D = current_car if is_instance_valid(current_car) else hero_car
	var position_to_save: Vector3 = _actor_position()
	if is_instance_valid(current_car): position_to_save = _safe_exit_position(current_car)
	var data: Dictionary = {"cash":cash,"mission_index":missions.mission_index,"mission_stage":missions.stage,
		"position":position_to_save,"car_position":parked.position,"car_yaw":parked.rotation.y,
		"health":player.health,"time_of_day":time_of_day,"collectibles":collected}
	if SaveStore.write_save(data):
		has_save = true
		save_clock = 0
	else: _notify("存档写入失败", "当前进度仍在本次游戏中，请检查磁盘空间。")

func _recover() -> void:
	if wanted>0:
		_notify("追捕中无法救援", "先脱离警方视线。")
		return
	if is_instance_valid(current_car):
		var at: Vector3 = _road_point(current_car.position)
		current_car.reset_at(at+Vector3.UP*0.2,current_car.rotation.y)
		current_car.repair()
	else:
		var at: Vector3 = _road_point(player.position)
		hero_car.reset_at(at+Vector3(0,0.3,-6),0)
		hero_car.repair()
		player.position = at+Vector3(3,0.3,-6)
	cash = maxi(0,cash-75)
	_notify("道路救援完成", "服务费 $75 · 车辆已移到附近道路。")

func _respawn() -> void:
	if is_instance_valid(current_car): _exit_car()
	player.reset_at(START+Vector3(3,0,1))
	hero_car.reset_at(START,0)
	hero_car.repair()
	cash = maxi(0,cash-200)
	heat = 0
	wanted = 0
	missions.restore(missions.mission_index,0)
	camera.position = player.position+Vector3(0,4,8)
	_notify("重新出发", "医疗费用 $200 · 当前任务已回到安全检查点。")
	_save()

func _joy_connection(device: int, connected: bool) -> void:
	if connected:
		active_pad = device
		_notify("手柄已连接", "左摇杆移动 · 右摇杆视角 · Y 交互 · Menu 暂停")
	elif active_pad==device:
		active_pad = -1
		using_controller = false
		if mode=="playing": _set_mode("paused")
		_notify("手柄已断开", "已暂停；可使用键鼠继续。")

func _actor_position() -> Vector3:
	return current_car.global_position if is_instance_valid(current_car) else player.global_position

func _decode_position(value: Variant, fallback: Vector3) -> Vector3:
	if value is Array and value.size()==3: return Vector3(float(value[0]),float(value[1]),float(value[2]))
	return fallback

func _notify(text: String, subtitle: String = "") -> void:
	if is_instance_valid(hud): hud.notify(text,subtitle)

func _radio_name() -> String:
	return ["NEON FM  /  98.6", "COASTLINE  /  103.2", "电台关闭"][radio_station]

func _capture(label: String, test_capture: bool) -> void:
	if total_time-_last_capture_time<1.0 or DisplayServer.get_name()=="headless": return
	_last_capture_time = total_time
	var hide_interface: bool = mode=="photo" and not test_capture
	if hide_interface:
		hud.hide()
		await get_tree().process_frame
	await RenderingServer.frame_post_draw
	var folder: String = "res://artifacts" if test_capture else "user://screenshots"
	DirAccess.make_dir_recursive_absolute(folder)
	var filename: String = label+".png" if test_capture else "NeonCoast_"+Time.get_datetime_string_from_system().replace(":","-")+".png"
	var path: String = folder.path_join(filename)
	var result: Error = get_viewport().get_texture().get_image().save_png(path)
	if hide_interface: hud.show()
	if result==OK:
		print("SCREENSHOT_SAVED ",ProjectSettings.globalize_path(path))
		if not test_capture: _notify("照片已保存",ProjectSettings.globalize_path(path))

func _notification(what: int) -> void:
	if what==NOTIFICATION_WM_CLOSE_REQUEST:
		_save()
		get_tree().quit()
	elif what==NOTIFICATION_APPLICATION_FOCUS_OUT and mode=="playing" and not _test_mode:
		_set_mode("paused")

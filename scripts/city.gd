extends Node3D
## An original, deterministic, entirely procedural coastal city.
## Every repeated surface is instanced by material instead of creating nodes per window.

const ROAD_SPACING: float = 72.0
const ROAD_WIDTH: float = 16.0
const ROAD_LINES: Array[float] = [-216.0, -144.0, -72.0, 0.0, 72.0, 144.0, 216.0]

var _rng: RandomNumberGenerator = RandomNumberGenerator.new()
var _materials: Dictionary = {}
var _batches: Dictionary = {}
var _meshes: Dictionary = {}
var _map_blocks: Array = []
var _world_environment: WorldEnvironment
var _sun: DirectionalLight3D
var _sky_material: ShaderMaterial
var _water_material: ShaderMaterial
var _street_lights: Array[OmniLight3D] = []
var _collision_body: StaticBody3D
var _hour: float = 17.6
var _built: bool = false

func _ready() -> void:
	_rng.seed = 65912026
	_create_materials()
	_create_meshes()
	_create_environment()
	_collision_body = StaticBody3D.new()
	_collision_body.name = "CityCollision"
	_collision_body.collision_layer = 1
	_collision_body.collision_mask = 0
	add_child(_collision_body)
	_create_ground_and_roads()
	_create_blocks()
	_create_street_furniture()
	_create_coast()
	_create_distant_skyline()
	_flush_batches()
	_built = true
	set_time_of_day(_hour)

func _create_materials() -> void:
	_material("asphalt", Color("333b48"), 0.96)
	_material("road_patch", Color("3a424f"), 0.98)
	_material("sidewalk", Color("b7b5a8"), 0.94)
	_material("curb", Color("e1d7bd"), 0.85)
	_material("white", Color("e5dec8"), 0.7)
	_material("yellow", Color("e8c559"), 0.8)
	_material("cream", Color("e3d6bc"), 0.78)
	_material("peach", Color("dca18c"), 0.8)
	_material("aqua", Color("94c9c0"), 0.75)
	_material("pink", Color("c48d99"), 0.8)
	_material("stone", Color("9bafb0"), 0.8)
	_material("navy", Color("263f50"), 0.62)
	_material("roof", Color("657479"), 0.85)
	_material("glass", Color("183445"), 0.31, 0.24)
	_material("glass_blue", Color("315e68"), 0.31, 0.28)
	_material("window_warm", Color("c9a567"), 0.4, 0.1, Color("ffca7d"), 0.6)
	_material("window_cool", Color("94c6c4"), 0.35, 0.1, Color("acd9db"), 0.35)
	_material("neon_pink", Color("ed819c"), 0.35, 0.0, Color("ff5b96"), 2.0)
	_material("neon_cyan", Color("82e3d0"), 0.3, 0.0, Color("61f2d7"), 1.6)
	_material("lamp", Color("ffe5b1"), 0.3, 0.0, Color("ffcc83"), 2.5)
	_material("metal", Color("3c5059"), 0.5, 0.65)
	_material("sand", Color("d9c59c"), 1.0)
	_material("wet_sand", Color("aaae92"), 0.8)
	_material("foam", Color("cadbd2"), 0.6)
	_material("wood", Color("987f62"), 0.85)
	_material("grass", Color("648a64"), 1.0)
	_material("grass_dark", Color("3f6950"), 1.0)
	_material("palm", Color("6b8f57"), 0.95)
	_material("palm_light", Color("89a766"), 0.95)
	_material("trunk", Color("867359"), 0.95)
	_material("red", Color("c95f54"), 0.75)
	_material("pool", Color("4bb3b2"), 0.2, 0.15, Color("24777b"), 0.12)
	_material("sign", Color("244c49"), 0.65)
	(_materials["palm"] as StandardMaterial3D).cull_mode = BaseMaterial3D.CULL_DISABLED
	(_materials["palm_light"] as StandardMaterial3D).cull_mode = BaseMaterial3D.CULL_DISABLED
	_surface_grain("asphalt", 41027, 0.26, 0.22, 0.93, 0.18)
	_surface_grain("sand", 91503, 0.62, 0.16, 0.95, 0.12)

func _surface_grain(material_key: String, seed_value: int, world_scale: float, frequency: float, low_value: float, normal_strength: float) -> void:
	# Two small, deterministic shared textures. World-space projection keeps grain
	# consistent on large scaled boxes, and mipmaps suppress distant shimmer.
	var noise: FastNoiseLite = FastNoiseLite.new()
	noise.seed = seed_value
	noise.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	noise.frequency = frequency
	noise.fractal_octaves = 3
	var ramp: Gradient = Gradient.new()
	ramp.set_color(0,Color(low_value,low_value,low_value,1.0))
	ramp.set_color(1,Color.WHITE)
	var albedo: NoiseTexture2D = NoiseTexture2D.new()
	albedo.width = 256
	albedo.height = 256
	albedo.noise = noise
	albedo.seamless = true
	albedo.generate_mipmaps = true
	albedo.color_ramp = ramp
	var normal: NoiseTexture2D = NoiseTexture2D.new()
	normal.width = 256
	normal.height = 256
	normal.noise = noise
	normal.seamless = true
	normal.generate_mipmaps = true
	normal.as_normal_map = true
	normal.bump_strength = 0.18
	var material: StandardMaterial3D = _materials[material_key]
	material.albedo_texture = albedo
	material.normal_enabled = true
	material.normal_texture = normal
	material.normal_scale = normal_strength
	material.uv1_triplanar = true
	material.uv1_world_triplanar = true
	material.uv1_scale = Vector3.ONE*world_scale
	material.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC

func _material(key: String, color: Color, roughness: float, metallic: float = 0.0, emission: Color = Color.BLACK, energy: float = 0.0) -> void:
	var mat: StandardMaterial3D = StandardMaterial3D.new()
	mat.albedo_color = color
	mat.roughness = roughness
	mat.metallic = metallic
	if energy > 0.0:
		mat.emission_enabled = true
		mat.emission = emission
		mat.emission_energy_multiplier = energy
	_materials[key] = mat

func _create_meshes() -> void:
	var cube: BoxMesh = BoxMesh.new()
	cube.size = Vector3.ONE
	_meshes["box"] = cube
	var cylinder: CylinderMesh = CylinderMesh.new()
	cylinder.top_radius = 0.5
	cylinder.bottom_radius = 0.5
	cylinder.height = 1.0
	cylinder.radial_segments = 10
	cylinder.rings = 1
	_meshes["cylinder"] = cylinder
	var cone: CylinderMesh = CylinderMesh.new()
	cone.top_radius = 0.0
	cone.bottom_radius = 0.5
	cone.height = 1.0
	cone.radial_segments = 12
	cone.rings = 1
	_meshes["cone"] = cone
	var sphere: SphereMesh = SphereMesh.new()
	sphere.radius = 0.5
	sphere.height = 1.0
	sphere.radial_segments = 10
	sphere.rings = 6
	_meshes["sphere"] = sphere
	var frond: SurfaceTool = SurfaceTool.new()
	frond.begin(Mesh.PRIMITIVE_TRIANGLES)
	for i in range(18):
		var t0: float = float(i) / 18.0
		var t1: float = float(i + 1) / 18.0
		var width0: float = sin(t0 * PI) * 0.69 + 0.025
		var width1: float = sin(t1 * PI) * 0.69 + 0.01
		var y0: float = sin(t0 * PI) * 1.0 - t0 * t0 * 1.8
		var y1: float = sin(t1 * PI) * 1.0 - t1 * t1 * 1.8
		# Feathered leaves, with a thin curved central rib and individual pointed leaflets.
		var points: Array[Vector3] = [Vector3(-0.035,y0,t0*5.0),Vector3(0.035,y0,t0*5.0),Vector3(0.025,y1,t1*5.0),Vector3(-0.035,y0,t0*5.0),Vector3(0.025,y1,t1*5.0),Vector3(-0.025,y1,t1*5.0)]
		points.append_array([Vector3(0.0,y0,t0*5.0),Vector3(width0,y1-0.12,t1*5.0+0.22),Vector3(0.0,y1,t1*5.0),Vector3(0.0,y0,t0*5.0),Vector3(0.0,y1,t1*5.0),Vector3(-width1,y1-0.12,t1*5.0+0.22)])
		for point: Vector3 in points:
			frond.set_normal(Vector3.UP)
			frond.add_vertex(point)
	_meshes["frond"] = frond.commit()

func _add_instance(mesh_key: String, material_key: String, position: Vector3, size: Vector3, rotation: Vector3 = Vector3.ZERO) -> void:
	var key: String = mesh_key + ":" + material_key
	if not _batches.has(key):
		_batches[key] = {"mesh": mesh_key, "material": material_key, "transforms": []}
	var basis: Basis = Basis.from_euler(rotation).scaled(size)
	(_batches[key]["transforms"] as Array).append(Transform3D(basis, position))

func _box(position: Vector3, size: Vector3, material: String, collision: bool = false, rotation_y: float = 0.0) -> void:
	_add_instance("box", material, position, size, Vector3(0.0, rotation_y, 0.0))
	if collision:
		var shape: BoxShape3D = BoxShape3D.new()
		shape.size = size
		var collider: CollisionShape3D = CollisionShape3D.new()
		collider.shape = shape
		collider.position = position
		collider.rotation.y = rotation_y
		_collision_body.add_child(collider)

func _beam(a: Vector3, b: Vector3, radius: float, material: String) -> void:
	var delta: Vector3 = b - a
	var direction: Vector3 = delta.normalized()
	var tangent: Vector3 = Vector3.RIGHT
	if absf(direction.dot(tangent)) > 0.95:
		tangent = Vector3.FORWARD
	var z_axis: Vector3 = tangent.cross(direction).normalized()
	var x_axis: Vector3 = direction.cross(z_axis).normalized()
	var basis: Basis = Basis(x_axis * radius * 2.0, direction * delta.length(), z_axis * radius * 2.0)
	var key: String = "cylinder:" + material
	if not _batches.has(key):
		_batches[key] = {"mesh": "cylinder", "material": material, "transforms": []}
	(_batches[key]["transforms"] as Array).append(Transform3D(basis, (a+b)*0.5))

func _flush_batches() -> void:
	for key: String in _batches:
		var batch: Dictionary = _batches[key]
		var transforms: Array = batch["transforms"]
		var multimesh: MultiMesh = MultiMesh.new()
		multimesh.transform_format = MultiMesh.TRANSFORM_3D
		multimesh.mesh = _meshes[batch["mesh"]]
		multimesh.instance_count = transforms.size()
		for i in range(transforms.size()):
			multimesh.set_instance_transform(i, transforms[i])
		var instance: MultiMeshInstance3D = MultiMeshInstance3D.new()
		instance.name = key.replace(":", "_")
		instance.multimesh = multimesh
		instance.material_override = _materials[batch["material"]]
		if batch["material"] in ["asphalt", "road_patch", "sidewalk", "curb", "sand", "wet_sand", "foam", "grass", "glass", "glass_blue", "window_warm", "window_cool", "white", "yellow", "neon_pink", "neon_cyan", "lamp"]:
			instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		add_child(instance)
	_batches.clear()

func _create_environment() -> void:
	_world_environment = WorldEnvironment.new()
	_world_environment.name = "CoastalAtmosphere"
	var environment: Environment = Environment.new()
	environment.background_mode = Environment.BG_SKY
	var sky: Sky = Sky.new()
	_sky_material = ShaderMaterial.new()
	_sky_material.shader = load("res://assets/shaders/world_sky.gdshader") as Shader
	sky.sky_material = _sky_material
	sky.radiance_size = Sky.RADIANCE_SIZE_128
	environment.sky = sky
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color("9db7c2")
	environment.ambient_light_energy = 0.4
	environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	environment.tonemap_exposure = 0.9
	environment.glow_enabled = true
	environment.glow_intensity = 0.65
	environment.glow_bloom = 0.1
	environment.fog_enabled = true
	environment.fog_light_color = Color("b7a59e")
	environment.fog_density = 0.00115
	environment.fog_sky_affect = 0.1
	_world_environment.environment = environment
	add_child(_world_environment)
	_sun = DirectionalLight3D.new()
	_sun.name = "GoldenHourSun"
	_sun.light_color = Color("ffca96")
	_sun.light_energy = 1.05
	_sun.shadow_enabled = true
	_sun.directional_shadow_max_distance = 155.0
	_sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_2_SPLITS
	_sun.shadow_bias = 0.12
	_sun.shadow_normal_bias = 1.5
	add_child(_sun)

func _create_ground_and_roads() -> void:
	_box(Vector3(0.0,-0.4,0.0), Vector3(520.0,0.8,520.0), "asphalt", true)
	for gx: int in range(6):
		for gz: int in range(6):
			var x: float = -180.0 + gx * ROAD_SPACING
			var z: float = -180.0 + gz * ROAD_SPACING
			_box(Vector3(x,0.08,z), Vector3(56.0,0.16,56.0), "sidewalk")
			# Pale raised perimeter reads clearly at street level without blocking the driving grid.
			_box(Vector3(x,0.1,z-27.85), Vector3(56.0,0.2,0.3), "curb")
			_box(Vector3(x,0.1,z+27.85), Vector3(56.0,0.2,0.3), "curb")
			_box(Vector3(x-27.85,0.1,z), Vector3(0.3,0.2,56.0), "curb")
			_box(Vector3(x+27.85,0.1,z), Vector3(0.3,0.2,56.0), "curb")
			for seam: float in [-18.0,-9.0,0.0,9.0,18.0]:
				_box(Vector3(x+seam,0.165,z),Vector3(0.025,0.01,55.8),"stone")
				_box(Vector3(x,0.165,z+seam),Vector3(55.8,0.01,0.025),"stone")
	for line: float in ROAD_LINES:
		for along: int in range(-249,250,9):
			var near_crossing: bool = false
			for crossing: float in ROAD_LINES:
				if absf(float(along)-crossing) < 13.0:
					near_crossing = true
			if not near_crossing:
				for offset: float in [-0.18,0.18]:
					_box(Vector3(line+offset,0.016,float(along)), Vector3(0.12,0.02,4.8), "yellow")
					_box(Vector3(float(along),0.017,line+offset), Vector3(4.8,0.02,0.12), "yellow")
				for offset: float in [-7.2,7.2]:
					_box(Vector3(line+offset,0.016,float(along)),Vector3(0.12,0.02,8.8),"white")
					_box(Vector3(float(along),0.017,line+offset),Vector3(8.8,0.02,0.12),"white")
		for crossing: float in ROAD_LINES:
			for stripe: int in range(-5,6,2):
				for side: float in [-11.0,11.0]:
					_box(Vector3(line+float(stripe),0.025,crossing+side),Vector3(0.95,0.025,3.4),"white")
					_box(Vector3(line+side,0.026,crossing+float(stripe)),Vector3(3.4,0.025,0.95),"white")
	# Outer parks contain the grid and frame the ocean promenade.
	_box(Vector3(-240.0,0.08,0.0),Vector3(29.0,0.16,510.0),"sidewalk")
	_box(Vector3(-244.0,0.18,0.0),Vector3(13.0,0.15,500.0),"grass")
	for z: float in [-240.0,240.0]:
		_box(Vector3(0.0,0.08,z),Vector3(449.0,0.16,29.0),"sidewalk")
		_box(Vector3(0.0,0.18,z),Vector3(430.0,0.15,14.0),"grass")

func _create_blocks() -> void:
	for gx: int in range(6):
		for gz: int in range(6):
			var center: Vector3 = Vector3(-180.0+gx*72.0,0.0,-180.0+gz*72.0)
			if (gx == 3 and gz == 2) or (gx == 1 and gz == 4):
				_create_park(center, gx == 3)
				continue
			if gx == 5:
				_create_hotel(center, gz)
				continue
			if (gx == 1 and gz == 1) or (gx == 2 and gz == 0):
				_create_tower(center, gz)
				continue
			var large: bool = (gx + gz) % 5 == 0
			if large:
				_building(center + Vector3(-11.0,0.0,0.0), 22.0,43.0, _rng.randi_range(4,8), gx+gz)
				_building(center + Vector3(14.0,0.0,-13.0), 18.0,18.0, _rng.randi_range(2,4), gx+gz+1)
				_building(center + Vector3(14.0,0.0,12.0), 18.0,22.0, _rng.randi_range(2,4), gx+gz+2)
			else:
				for ox: float in [-13.2,13.2]:
					for oz: float in [-13.2,13.2]:
						var floors: int = _rng.randi_range(2,5) if gx >= 3 else _rng.randi_range(3,10)
						_building(center + Vector3(ox,0.0,oz),_rng.randf_range(18.0,21.0),_rng.randf_range(18.0,21.0),floors,_rng.randi_range(0,11))
			for corner: Vector2 in [Vector2(-24.0,-24.0),Vector2(24.0,24.0)]:
				_palm(center + Vector3(corner.x,0.17,corner.y), _rng.randf_range(7.5,10.5))

func _building(pos: Vector3, width: float, depth: float, floors: int, style: int) -> void:
	var palettes: Array[String] = ["cream","peach","aqua","stone","pink","cream"]
	var facade: String = palettes[posmod(style,palettes.size())]
	var height: float = 3.2 + float(floors)*3.4
	_box(pos + Vector3(0.0,height*0.5+0.16,0.0),Vector3(width,height,depth),facade,true)
	_box(pos + Vector3(0.0,0.4,0.0),Vector3(width+0.25,0.45,depth+0.25),"stone")
	_box(pos + Vector3(0.0,height+0.28,0.0),Vector3(width+0.5,0.4,depth+0.5),"cream")
	_box(pos + Vector3(0.0,height+0.5,0.0),Vector3(width-0.6,0.16,depth-0.6),"roof")
	_map_blocks.append({"x":pos.x,"z":pos.z,"w":width,"d":depth,"color":(_materials[facade] as StandardMaterial3D).albedo_color})
	for level: int in range(floors):
		var y: float = 5.1 + float(level)*3.4
		for axis: int in range(2):
			var face_width: float = width if axis == 0 else depth
			var count: int = maxi(2, int((face_width-2.0)/2.9))
			for column: int in range(count):
				var horizontal: float = (float(column)-float(count-1)*0.5)*2.9
				var pick: float = _rng.randf()
				var glass: String = "window_warm" if pick < 0.22 else ("window_cool" if pick < 0.3 else "glass")
				for side: float in [-1.0,1.0]:
					var window_pos: Vector3 = Vector3(horizontal,y,side*(depth*0.5+0.035)) if axis == 0 else Vector3(side*(width*0.5+0.035),y,horizontal)
					var window_size: Vector3 = Vector3(1.65,1.95,0.08) if axis == 0 else Vector3(0.08,1.95,1.65)
					_box(pos+window_pos,window_size,glass)
		if style % 3 == 0:
			_box(pos+Vector3(0.0,y-1.3,0.0),Vector3(width+0.2,0.15,depth+0.2),"cream")
		if style % 4 == 1 and level % 2 == 0:
			for side: float in [-1.0,1.0]:
				_box(pos+Vector3(0.0,y-1.2,side*(depth*0.5+0.55)),Vector3(width*0.78,0.16,1.2),"cream")
				_box(pos+Vector3(0.0,y-0.65,side*(depth*0.5+1.1)),Vector3(width*0.78,0.8,0.08),"glass_blue")
	# Ground-floor storefront ribbons, pilasters and a clearly visible entrance.
	for side: float in [-1.0,1.0]:
		_box(pos+Vector3(0.0,1.8,side*(depth*0.5+0.04)),Vector3(width-2.2,2.6,0.1),"glass_blue")
		for x: float in [-width*0.25,0.0,width*0.25]:
			_box(pos+Vector3(x,1.8,side*(depth*0.5+0.1)),Vector3(0.16,2.7,0.15),"cream")
		_box(pos+Vector3(0.0,3.28,side*(depth*0.5+0.6)),Vector3(width-1.4,0.24,1.3),"aqua" if style%2==0 else "peach")
	for side: float in [-1.0,1.0]:
		_box(pos+Vector3(side*(width*0.5-0.55),height*0.5,depth*0.5+0.1),Vector3(0.4,height,0.25),"cream")
	_box(pos+Vector3(width*0.2,height+1.0,-depth*0.16),Vector3(3.6,1.1,2.5),"stone")
	_box(pos+Vector3(-width*0.19,height+0.8,depth*0.12),Vector3(2.1,0.65,2.1),"metal")
	if style % 5 == 0:
		_box(pos+Vector3(0.0,height+0.63,depth*0.5),Vector3(width*0.88,0.12,0.16),"neon_cyan")

func _create_hotel(center: Vector3, index: int) -> void:
	var floors: int = [5,7,4,8,5,6][index]
	var height: float = 3.2 + floors*3.4
	_building(center + Vector3(-7.0,0.0,0.0),30.0,43.0,floors,index)
	# A central art-deco blade, stepped crown, and an inviting motor court.
	_box(center+Vector3(-7.0,height+2.0,0.0),Vector3(19.0,3.3,29.0),"cream")
	_box(center+Vector3(-7.0,height+4.5,0.0),Vector3(10.0,2.0,21.0),"aqua" if index%2==0 else "peach")
	_box(center+Vector3(8.1,height*0.5+1.0,0.0),Vector3(0.6,height+4.0,3.3),"cream")
	for z: float in [-1.0,0.0,1.0]:
		_box(center+Vector3(8.48,height*0.5+1.0,z),Vector3(0.12,height+3.3,0.16),"neon_pink" if index%2==0 else "neon_cyan")
	_box(center+Vector3(14.0,0.2,0.0),Vector3(9.0,0.15,21.0),"white")
	_box(center+Vector3(14.0,0.29,0.0),Vector3(6.5,0.08,17.0),"pool")
	for z: float in [-9.4,9.4]:
		for x: float in [10.0,18.0]:
			_box(center+Vector3(x,0.7,z),Vector3(0.08,1.0,0.08),"metal")
		_box(center+Vector3(14.0,1.2,z),Vector3(8.1,0.08,0.08),"metal")
	for z: float in [-7.0,-2.3,2.3,7.0]:
		_box(center+Vector3(20.0,0.42,z),Vector3(1.5,0.28,0.7),"white")
		_box(center+Vector3(20.6,0.65,z),Vector3(0.25,0.6,0.7),"aqua")
	_box(center+Vector3(11.0,3.8,20.0),Vector3(12.0,0.4,4.0),"cream")
	for z: float in [-23.5,23.5]:
		_palm(center+Vector3(21.5,0.16,z),10.0)
	for z: float in [-12.0,12.0]:
		_palm(center+Vector3(23.0,0.16,z),8.5)
	for z: float in [-18.5,18.5]:
		_box(center+Vector3(23.0,0.55,z),Vector3(1.7,0.75,3.0),"cream")
		_add_instance("sphere","grass_dark",center+Vector3(23.0,1.2,z),Vector3(1.8,1.2,3.0))
	var names: Array[String] = ["THE MIRAGE","CORAL PALMS","SOLSTICE","AZURE HOUSE","THE MARLIN","PALOMA"]
	_sign(names[index], center+Vector3(8.65,height-1.2,0.0), Vector3(0.0,PI*0.5,0.0), 64, Color("ffe2a3"), 0.045)
	_sign("HOTEL  ·  OCEAN CLUB",center+Vector3(-7.0,3.8,21.65),Vector3.ZERO,34,Color("e9d9b8"),0.025)

func _create_tower(center: Vector3, variant: int) -> void:
	var height: float = 76.0 if variant == 1 else 92.0
	_box(center+Vector3(0.0,4.1,0.0),Vector3(45.0,8.0,42.0),"cream",true)
	_box(center+Vector3(0.0,height*0.5,0.0),Vector3(29.0,height,29.0),"navy",true)
	_box(center+Vector3(0.0,height+2.5,0.0),Vector3(25.0,5.0,25.0),"glass_blue")
	_box(center+Vector3(0.0,height+6.1,0.0),Vector3(19.0,2.0,19.0),"cream")
	_map_blocks.append({"x":center.x,"z":center.z,"w":45.0,"d":42.0,"color":Color("537b87")})
	for floor_index: int in range(2,int(height/3.5)):
		var y: float = float(floor_index)*3.5
		for side: float in [-1.0,1.0]:
			_box(center+Vector3(0.0,y,side*14.55),Vector3(28.5,2.6,0.08),"glass_blue")
			_box(center+Vector3(side*14.55,y,0.0),Vector3(0.08,2.6,28.5),"glass_blue")
			for column: int in range(-4,5):
				if _rng.randf()<0.24:
					_box(center+Vector3(float(column)*3.0,y,side*14.61),Vector3(2.5,2.25,0.05),"window_warm")
	for x: float in [-14.5,-7.25,0.0,7.25,14.5]:
		for side: float in [-1.0,1.0]:
			_box(center+Vector3(x,height*0.5,side*14.65),Vector3(0.22,height,0.18),"stone")
			_box(center+Vector3(side*14.65,height*0.5,x),Vector3(0.18,height,0.22),"stone")
	for x: float in [-14.7,14.7]:
		for z: float in [-14.7,14.7]:
			_box(center+Vector3(x,height*0.5,z),Vector3(0.16,height+0.5,0.16),"neon_cyan")
	for side: float in [-1.0,1.0]:
		_box(center+Vector3(0.0,4.0,side*21.1),Vector3(41.0,5.0,0.1),"glass")
		for x: float in [-19.0,19.0]:
			_palm(center+Vector3(x,0.16,side*24.0),9.5)
	_sign("MERIDIAN" if variant==1 else "VESPER",center+Vector3(0.0,height+3.0,12.65),Vector3.ZERO,72,Color("9cf9e1"),0.055)

func _create_park(center: Vector3, fountain: bool) -> void:
	_box(center+Vector3(0.0,0.2,0.0),Vector3(45.0,0.1,45.0),"grass")
	_box(center+Vector3(0.0,0.27,0.0),Vector3(7.0,0.08,52.0),"sand")
	_box(center+Vector3(0.0,0.28,0.0),Vector3(52.0,0.08,7.0),"sand")
	_map_blocks.append({"x":center.x,"z":center.z,"w":48.0,"d":48.0,"color":Color("477764")})
	if fountain:
		_add_instance("cylinder","cream",center+Vector3(0.0,0.6,0.0),Vector3(13.0,0.65,13.0))
		_add_instance("cylinder","pool",center+Vector3(0.0,0.95,0.0),Vector3(11.5,0.08,11.5))
		_add_instance("cylinder","stone",center+Vector3(0.0,2.0,0.0),Vector3(1.0,2.6,1.0))
		_add_instance("sphere","neon_cyan",center+Vector3(0.0,3.6,0.0),Vector3(2.5,2.5,2.5))
	else:
		_box(center+Vector3(0.0,0.4,0.0),Vector3(12.0,0.35,12.0),"white")
		_box(center+Vector3(0.0,3.0,0.0),Vector3(1.3,5.4,1.3),"peach",false,0.6)
		_box(center+Vector3(0.0,4.0,0.0),Vector3(5.5,1.3,1.3),"aqua",false,0.6)
	for x: float in [-16.0,16.0]:
		for z: float in [-16.0,16.0]:
			_palm(center+Vector3(x,0.3,z),_rng.randf_range(9.0,12.0))
			for bx: float in [-3.0,3.0]:
				_add_instance("sphere","grass_dark",center+Vector3(x+bx,1.0,z),Vector3(3.5,1.8,3.5))
	for z: float in [-9.0,9.0]:
		_bench(center+Vector3(7.0,0.25,z),PI*0.5)
		_bench(center+Vector3(-7.0,0.25,z),-PI*0.5)

func _palm(position: Vector3, height: float) -> void:
	var sway: float = _rng.randf_range(-0.8,0.8)
	var p0: Vector3 = position
	var p1: Vector3 = position+Vector3(sway*0.3,height*0.45,0.15)
	var p2: Vector3 = position+Vector3(sway*0.7,height*0.77,0.35)
	var top: Vector3 = position+Vector3(sway,height,0.55)
	_beam(p0,p1,0.24,"trunk")
	_beam(p1,p2,0.19,"trunk")
	_beam(p2,top,0.14,"trunk")
	for leaf: int in range(8):
		var angle: float = float(leaf)*TAU/8.0 + sway
		var scale_value: float = _rng.randf_range(0.8,1.2)
		_add_instance("frond","palm" if leaf%2==0 else "palm_light",top,Vector3.ONE*scale_value,Vector3(_rng.randf_range(-0.18,0.18),angle,0.0))
	_add_instance("sphere","grass_dark",top+Vector3(0.0,-0.25,0.0),Vector3(0.75,0.9,0.75))
	_cylinder_collision(position+Vector3(sway*0.3,height*0.5,0.15),0.25,height)

func _cylinder_collision(position: Vector3, radius: float, height: float) -> void:
	var shape: CylinderShape3D = CylinderShape3D.new()
	shape.radius = radius
	shape.height = height
	var collider: CollisionShape3D = CollisionShape3D.new()
	collider.shape = shape
	collider.position = position
	_collision_body.add_child(collider)

func _bench(position: Vector3, yaw: float = 0.0) -> void:
	var basis: Basis = Basis(Vector3.UP,yaw)
	_box(position+basis*Vector3(0.0,0.55,0.0),Vector3(2.5,0.14,0.7),"wood",true,yaw)
	_box(position+basis*Vector3(0.0,1.05,0.3),Vector3(2.5,0.6,0.12),"wood",false,yaw)
	for x: float in [-0.9,0.9]:
		_box(position+basis*Vector3(x,0.25,0.0),Vector3(0.12,0.5,0.65),"metal",false,yaw)

func _street_lamp(position: Vector3, facing: float, lit: bool) -> void:
	var inward: Vector3 = Vector3(sin(facing),0.0,cos(facing))
	_beam(position,position+Vector3(0.0,7.2,0.0),0.10,"metal")
	_cylinder_collision(position+Vector3(0.0,3.6,0.0),0.12,7.2)
	_beam(position+Vector3(0.0,7.2,0.0),position+Vector3(0.0,7.7,0.0)+inward*2.0,0.08,"metal")
	_box(position+Vector3(0.0,7.6,0.0)+inward*2.0,Vector3(0.65,0.16,1.05),"metal",false,-facing)
	_box(position+Vector3(0.0,7.49,0.0)+inward*2.0,Vector3(0.5,0.05,0.8),"lamp",false,-facing)
	if lit:
		var lamp: OmniLight3D = OmniLight3D.new()
		lamp.position = position+Vector3(0.0,7.1,0.0)+inward*2.0
		lamp.light_color = Color("ffcb8b")
		lamp.light_energy = 1.3
		lamp.omni_range = 18.0
		lamp.omni_attenuation = 1.5
		lamp.shadow_enabled = false
		lamp.distance_fade_enabled = true
		lamp.distance_fade_begin = 70.0
		lamp.distance_fade_length = 25.0
		add_child(lamp)
		_street_lights.append(lamp)

func _create_street_furniture() -> void:
	for index: int in range(7):
		var line: float = ROAD_LINES[index]
		for block: int in range(6):
			var along: float = -180.0+float(block)*72.0
			_street_lamp(Vector3(line+10.5,0.16,along),-PI*0.5,index%2==0)
			_street_lamp(Vector3(along,0.16,line+10.5),PI,block%3==0)
			if index==6:
				_palm(Vector3(line+15.5,0.16,along-18.0),10.0)
				_palm(Vector3(line+15.5,0.16,along+18.0),11.2)
				_bench(Vector3(238.0,0.16,along),PI*0.5)
	# Sparse, correctly positioned traffic heads keep each crossroads leg readable.
	for ix: int in range(1,7,2):
		for iz: int in range(1,7,2):
			var x: float = ROAD_LINES[ix]
			var z: float = ROAD_LINES[iz]
			var base: Vector3 = Vector3(x+10.7,0.16,z+10.7)
			_beam(base,base+Vector3(0.0,5.3,0.0),0.09,"metal")
			_box(base+Vector3(0.0,4.75,-0.2),Vector3(0.45,1.2,0.35),"navy")
			_add_instance("sphere","red",base+Vector3(0.0,5.1,-0.4),Vector3(0.22,0.22,0.08))
			_add_instance("sphere","yellow",base+Vector3(0.0,4.75,-0.4),Vector3(0.22,0.22,0.08))
			_add_instance("sphere","neon_cyan",base+Vector3(0.0,4.4,-0.4),Vector3(0.22,0.22,0.08))
			_box(base+Vector3(0.0,3.25,0.0),Vector3(2.8,0.45,0.12),"sign")
	for z: int in range(-230,231,32):
		_palm(Vector3(-244.0,0.3,float(z)),_rng.randf_range(9.0,12.0))
	for x: int in range(-210,211,36):
		for z: float in [-241.0,241.0]:
			_palm(Vector3(float(x),0.3,z),_rng.randf_range(8.0,11.0))
	# A few distinctive local businesses; all names and geometry are original.
	_sign("CAFÉ LUNA",Vector3(36.0,5.2,132.0),Vector3.ZERO,58,Color("ffc5b0"),0.045)
	_sign("PALM GROVE",Vector3(-108.0,2.7,99.0),Vector3.ZERO,46,Color("d5e8c5"),0.04)

func _create_coast() -> void:
	_box(Vector3(239.5,0.1,0.0),Vector3(28.0,0.2,520.0),"sidewalk")
	_box(Vector3(250.0,0.05,0.0),Vector3(8.0,0.2,520.0),"wood",true)
	for z: int in range(-258,260,2):
		_box(Vector3(250.0,0.158,float(z)),Vector3(7.9,0.015,0.045),"sand")
	_box(Vector3(282.0,-0.12,0.0),Vector3(64.0,0.32,560.0),"sand",true)
	_box(Vector3(311.0,-0.2,0.0),Vector3(6.0,0.12,560.0),"wet_sand")
	_box(Vector3(315.0,-0.23,0.0),Vector3(1.5,0.04,560.0),"foam")
	for z: int in range(-240,241,40):
		_palm(Vector3(258.0,0.04,float(z)),_rng.randf_range(8.5,11.5))
		_beach_umbrella(Vector3(279.0,0.06,float(z)+8.0),"aqua" if z%80==0 else "peach")
		_beach_umbrella(Vector3(294.0,0.06,float(z)-6.0),"cream")
		for dz: float in [4.0,11.0]:
			_box(Vector3(282.5,0.35,float(z)+dz),Vector3(2.2,0.18,0.8),"white",false,-0.15)
			_box(Vector3(283.45,0.67,float(z)+dz),Vector3(0.65,0.75,0.8),"aqua",false,-0.15)
	for z: float in [-165.0,95.0]:
		_lifeguard_tower(Vector3(299.0,0.05,z))
	# A small pier punctuates the uninterrupted sea horizon.
	_box(Vector3(345.0,0.85,192.0),Vector3(90.0,0.5,7.0),"wood")
	for x: int in range(307,390,8):
		for z: float in [188.5,195.5]:
			_beam(Vector3(float(x),-2.0,z),Vector3(float(x),2.1,z),0.14,"wood")
	for z: float in [188.5,195.5]:
		_box(Vector3(347.0,2.0,z),Vector3(85.0,0.14,0.14),"white")
	_box(Vector3(389.0,0.85,192.0),Vector3(13.0,0.5,19.0),"wood")
	# Quiet yachts and sails supply scale without additional simulation.
	_boat(Vector3(380.0,-0.1,-105.0),0.3)
	_boat(Vector3(465.0,-0.1,45.0),-0.7)
	_boat(Vector3(560.0,-0.1,-250.0),0.9)
	var ocean: MeshInstance3D = MeshInstance3D.new()
	ocean.name = "AtlanticWater"
	var plane: PlaneMesh = PlaneMesh.new()
	plane.size = Vector2(6000.0,8000.0)
	plane.subdivide_width = 150
	plane.subdivide_depth = 150
	ocean.mesh = plane
	ocean.position = Vector3(3310.0,-0.34,0.0)
	_water_material = ShaderMaterial.new()
	_water_material.shader = load("res://assets/shaders/water.gdshader") as Shader
	ocean.material_override = _water_material
	ocean.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(ocean)
	_sign("NEON COAST",Vector3(250.0,5.7,-204.0),Vector3(0.0,-PI*0.5,0.0),84,Color("ffd5ac"),0.065)
	_sign("THE WORLD IS YOURS TO EXPLORE",Vector3(249.94,4.6,-204.0),Vector3(0.0,-PI*0.5,0.0),28,Color("bcdfd1"),0.028)

func _beach_umbrella(position: Vector3, fabric: String) -> void:
	_beam(position,position+Vector3(0.0,2.6,0.0),0.045,"wood")
	_add_instance("cone",fabric,position+Vector3(0.0,2.45,0.0),Vector3(4.8,1.0,4.8))
	_add_instance("cone","white",position+Vector3(0.0,2.72,0.0),Vector3(2.6,0.55,2.6))

func _lifeguard_tower(position: Vector3) -> void:
	for x: float in [-1.8,1.8]:
		for z: float in [-1.6,1.6]:
			_beam(position+Vector3(x,0.0,z),position+Vector3(x,2.3,z),0.14,"wood")
	_box(position+Vector3(0.0,2.2,0.0),Vector3(4.8,0.3,4.5),"cream")
	_box(position+Vector3(0.0,3.35,0.0),Vector3(3.7,2.0,3.3),"aqua")
	_box(position+Vector3(0.0,3.65,-1.67),Vector3(3.0,0.9,0.08),"glass")
	_box(position+Vector3(1.9,3.65,0.0),Vector3(0.08,0.9,2.5),"glass")
	_box(position+Vector3(0.0,4.45,0.0),Vector3(4.6,0.25,4.3),"red")
	for i: int in range(6):
		_box(position+Vector3(-2.1-float(i)*0.33,2.05-float(i)*0.32,0.0),Vector3(0.4,0.12,1.5),"wood")
	_sign("LIFEGUARD",position+Vector3(0.0,2.82,-1.72),Vector3(0.0,PI,0.0),32,Color.WHITE,0.023)

func _boat(position: Vector3, yaw: float) -> void:
	var basis: Basis = Basis(Vector3.UP,yaw)
	_add_instance("sphere","white",position+Vector3(0.0,0.5,0.0),Vector3(4.5,2.2,13.0),Vector3(0.0,yaw,0.0))
	_box(position+basis*Vector3(0.0,1.8,-1.0),Vector3(3.4,1.7,5.5),"cream",false,yaw)
	_box(position+basis*Vector3(0.0,2.4,-1.2),Vector3(3.5,0.6,4.9),"glass",false,yaw)
	_box(position+basis*Vector3(0.0,2.8,-1.0),Vector3(4.0,0.2,6.0),"white",false,yaw)
	_beam(position+Vector3(0.0,2.8,0.0),position+Vector3(0.0,8.5,0.0),0.06,"metal")

func _create_distant_skyline() -> void:
	for i: int in range(24):
		var x: float = -430.0 - _rng.randf_range(0.0,160.0)
		var z: float = -600.0 + float(i)*50.0
		var height: float = _rng.randf_range(28.0,110.0)
		_box(Vector3(x,height*0.5,z),Vector3(_rng.randf_range(25.0,45.0),height,_rng.randf_range(25.0,45.0)),"stone")
		_box(Vector3(x,height+1.5,z),Vector3(20.0,3.0,20.0),"navy")
	_box(Vector3(-470.0,-0.6,0.0),Vector3(460.0,1.0,1500.0),"grass_dark")

func _sign(text: String, position: Vector3, rotation: Vector3, font_size: int, color: Color, pixel_size: float) -> void:
	var label: Label3D = Label3D.new()
	label.text = text
	label.position = position
	label.rotation = rotation
	label.font_size = font_size
	label.pixel_size = pixel_size
	label.modulate = color
	label.outline_size = 2
	label.outline_modulate = Color(0.05,0.1,0.13,0.7)
	label.no_depth_test = false
	label.shaded = false
	label.double_sided = false
	label.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(label)

func get_district(pos: Vector3) -> String:
	if pos.x > 225.0:
		return "OCEAN PROMENADE"
	if pos.x > 140.0:
		return "OCEAN DRIVE"
	if pos.x < -65.0 and pos.z < 30.0:
		return "MERIDIAN DOWNTOWN"
	if pos.z > 135.0:
		return "SOUTH MARINA"
	if pos.z < -135.0:
		return "NORTH KEY"
	if pos.x < -65.0:
		return "PALM GROVE"
	return "ART DECO DISTRICT"

func set_time_of_day(hour: float) -> void:
	_hour = fposmod(hour,24.0)
	if not _built:
		return
	var elevation: float = sin((_hour-6.0)/24.0*TAU)
	var night: float = 1.0-smoothstep(-0.16,0.13,elevation)
	var day: float = smoothstep(0.25,0.7,elevation)
	var sun_direction: Vector3 = Vector3(0.86,maxf(elevation,0.16),-0.44).normalized()
	_sun.position = sun_direction*100.0
	_sun.look_at(Vector3.ZERO,Vector3.UP)
	_sun.light_energy = lerpf(0.08,1.05,1.0-night)
	_sun.light_color = Color("ffd4a4").lerp(Color("fff1da"),day).lerp(Color("8aa8df"),night)
	_sky_material.set_shader_parameter("sun_direction",sun_direction)
	_sky_material.set_shader_parameter("night_mix",night)
	_sky_material.set_shader_parameter("day_mix",day)
	_water_material.set_shader_parameter("night_mix",night)
	_world_environment.environment.ambient_light_energy = lerpf(0.4,0.26,night)
	_world_environment.environment.ambient_light_color = Color("a8bdc9").lerp(Color("8296ca"),night)
	_world_environment.environment.fog_light_color = Color("b7a59e").lerp(Color("506187"),night).lerp(Color("a8c0c7"),day)
	for lamp: OmniLight3D in _street_lights:
		lamp.light_energy = lerpf(0.8,1.8,night)

func get_map_blocks() -> Array:
	return _map_blocks.duplicate()

func is_road(pos: Vector3) -> bool:
	if absf(pos.x)>254.0 or absf(pos.z)>254.0:
		return false
	for line: float in ROAD_LINES:
		if absf(pos.x-line)<ROAD_WIDTH*0.5 or absf(pos.z-line)<ROAD_WIDTH*0.5:
			return true
	return false

func random_road_position(rng: RandomNumberGenerator) -> Vector3:
	var line: float = ROAD_LINES[rng.randi_range(0,ROAD_LINES.size()-1)]
	var along: float = rng.randf_range(-226.0,226.0)
	var lane: float = 3.8 if rng.randf()>0.5 else -3.8
	if rng.randf()>0.5:
		return Vector3(line+lane,0.0,along)
	return Vector3(along,0.0,line+lane)

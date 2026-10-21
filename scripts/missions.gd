extends Node
## A short original campaign. Every objective is spatial and playable.
signal completed(title: String, reward: int)
signal message(text: String, subtitle: String)
signal heat_requested(amount: float)

var mission_index: int = 0
var stage: int = 0
var elapsed: float = 0.0
var checkpoint: int = 0
var mission_time: float = 0.0
var finished: bool = false
var _position: Vector3 = Vector3.ZERO
var _vehicle: bool = false
var _wanted: int = 0
var _delivery_hold: float = 0.0
var _escape_seen: bool = false
const RACE: Array[Vector3] = [Vector3(144,0,144),Vector3(-144,0,144),Vector3(-144,0,-144),Vector3(144,0,-144),Vector3(216,0,-144),Vector3(216,0,144)]
const TITLES: Array[String] = ["01 / 海风来信", "02 / 潮汐账本", "03 / 落日跑线", "04 / 最后一程"]

func tick(delta: float, at: Vector3, in_car: bool, wanted: int, speed: float) -> void:
	_position = at
	_vehicle = in_car
	_wanted = wanted
	elapsed += delta
	if finished:
		return
	if mission_index == 0:
		if stage == 0 and in_car:
			stage = 1
			message.emit("马洛 · 新消息", "先到海岸大道取件。车里有你需要的一切。")
		elif stage == 2:
			if in_car and _near(Vector3(72,0,-144), 12.0) and absf(speed) < 2.5:
				_delivery_hold += delta
				if _delivery_hold > 1.2:
					_complete(750)
			else:
				_delivery_hold = 0.0
	elif mission_index == 1:
		if stage == 1:
			if wanted > 0:
				_escape_seen = true
			if _escape_seen and wanted == 0:
				stage = 2
				message.emit("追捕解除", "把账本交给南港码头的接头人。")
		elif stage == 2 and in_car and _near(Vector3(144,0,216), 12.0) and absf(speed) < 2.5:
			_complete(1800)
	elif mission_index == 2 and stage == 1:
		mission_time += delta
		if mission_time > 120.0:
			stage = 0
			checkpoint = 0
			message.emit("本次跑线超时", "返回起点重新挑战；不扣除现金。")
		elif in_car and _near(RACE[checkpoint], 15.0):
			checkpoint += 1
			if checkpoint >= RACE.size():
				_complete(1400 + int(maxf(0.0, 120.0 - mission_time) * 12.0))
			else:
				message.emit("检查点 %d / %d" % [checkpoint, RACE.size()], "继续沿道路前进 · 剩余 %d 秒" % int(120.0-mission_time))
	elif mission_index == 3:
		if stage == 1:
			if wanted > 0:
				_escape_seen = true
			if _escape_seen and wanted == 0:
				stage = 2
				message.emit("马洛 · 最后一条消息", "到棕榈公园。今晚，就到这里。")
		elif stage == 2 and in_car and _near(Vector3(0,0,144),12.0) and absf(speed) < 2.5:
			_complete(3500)

func interact(at: Vector3, in_car: bool) -> bool:
	_position = at
	_vehicle = in_car
	if finished:
		return false
	if mission_index == 0 and stage == 1 and _near(Vector3(216,0,-72),11.0):
		stage = 2
		message.emit("包裹已取走", "送到市中心交叉口，停稳车辆完成交付。")
		return true
	if mission_index == 1 and stage == 0 and _near(Vector3(-144,0,72),10.0):
		if in_car:
			return false
		stage = 1
		_escape_seen = false
		heat_requested.emit(48.0)
		message.emit("账本到手 · 警方已获悉", "返回车辆，拉开距离。视野外等待通缉消退。")
		return true
	if mission_index == 2 and stage == 0 and _near(Vector3(216,0,144),13.0) and in_car:
		stage = 1
		checkpoint = 0
		mission_time = 0.0
		message.emit("落日跑线 · 开始", "120 秒，六个检查点。留意路口，Shift / A 可加速。")
		return true
	if mission_index == 3 and stage == 0 and in_car and _near(Vector3(-144,0,-144),11.0):
		stage = 1
		_escape_seen = false
		heat_requested.emit(72.0)
		message.emit("最后的货物已装车", "摆脱巡逻队，再去棕榈公园。")
		return true
	return false

func status() -> Dictionary:
	var target: Vector3 = Vector3.ZERO
	var objective: String = ""
	var has_target: bool = true
	var prompt: String = ""
	var progress: float = 0.0
	if finished:
		return {"title":"自由漫游 / THE COAST IS YOURS", "objective":"探索城市，收集海岸明信片，或在暂停菜单重玩故事。", "target":target,"has_target":false,"progress":1.0,"prompt":""}
	match mission_index:
		0:
			match stage:
				0:
					target = Vector3(216,0,30)
					objective = "进入身旁的 Comet GT · F / Y 上车"
				1:
					target = Vector3(216,0,-72)
					objective = "前往海岸大道标记，停稳后取走包裹"
					if _near(target,11.0): prompt = "取走包裹"
				2:
					target = Vector3(72,0,-144)
					objective = "将包裹送往市中心，在金色区域内停稳"
			progress = float(stage) / 3.0
		1:
			match stage:
				0:
					target = Vector3(-144,0,72)
					objective = "前往小哈瓦那，下车后取走潮汐账本"
					if _near(target,10.0) and not _vehicle: prompt = "拿起账本"
				1:
					has_target = false
					objective = "甩开巡逻车 · 远离警方视线，等待通缉星消失"
				2:
					target = Vector3(144,0,216)
					objective = "前往南港码头，停稳后交付账本"
			progress = float(stage) / 3.0
		2:
			if stage == 0:
				target = Vector3(216,0,144)
				objective = "驾车前往海岸起点，按 F / Y 开始计时挑战"
				if _near(target,13.0) and _vehicle: prompt = "开始落日跑线"
			else:
				target = RACE[mini(checkpoint, RACE.size()-1)]
				objective = "检查点 %d / 6 · 剩余 %d 秒" % [checkpoint+1, int(120.0-mission_time)]
				progress = float(checkpoint)/6.0
		3:
			match stage:
				0:
					target = Vector3(-144,0,-144)
					objective = "驾车前往北城仓库，按 F / Y 装载最后一批货物"
					if _near(target,11.0) and _vehicle: prompt = "接收货物"
				1:
					has_target = false
					objective = "甩开三支巡逻队 · 利用路口与建筑切断视线"
				2:
					target = Vector3(0,0,144)
					objective = "前往棕榈公园，停稳车辆结束最后一程"
			progress = float(stage)/3.0
	return {"title":TITLES[clampi(mission_index,0,3)],"objective":objective,"target":target,"has_target":has_target,"progress":progress,"prompt":prompt}

func restore(index: int, stage_value: int) -> void:
	mission_index = clampi(index,0,4)
	stage = clampi(stage_value,0,2)
	finished = mission_index >= 4
	# Timed races and pursuit scenes restart at a safe checkpoint on load.
	if mission_index == 2:
		stage = 0
	if mission_index in [1,3] and stage == 1:
		stage = 0
	checkpoint = 0
	mission_time = 0.0
	_delivery_hold = 0.0
	_escape_seen = false

func _near(target: Vector3, radius: float) -> bool:
	return Vector2(_position.x-target.x,_position.z-target.z).length() <= radius

func _complete(reward: int) -> void:
	var title: String = TITLES[clampi(mission_index,0,3)]
	mission_index += 1
	stage = 0
	checkpoint = 0
	_delivery_hold = 0.0
	_escape_seen = false
	finished = mission_index >= 4
	completed.emit(title,reward)

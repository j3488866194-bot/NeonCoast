extends SceneTree
## Run: Godot --headless --path . --script res://tests/missions_smoke.gd
## Deterministic campaign integration: no rendering, actors, or external resources needed.

const MISSIONS = preload("res://scripts/missions.gd")
const START: Vector3 = Vector3(216,0,30)
const PACKAGE: Vector3 = Vector3(216,0,-72)
const DELIVERY: Vector3 = Vector3(72,0,-144)
const LEDGER: Vector3 = Vector3(-144,0,72)
const MARINA: Vector3 = Vector3(144,0,216)
const RACE_START: Vector3 = Vector3(216,0,144)
const CARGO: Vector3 = Vector3(-144,0,-144)
const PARK: Vector3 = Vector3(0,0,144)

var failures: Array[String] = []
var checks: int = 0
var rewards: Array[int] = []
var titles: Array[String] = []
var heat: Array[float] = []

func _initialize() -> void:
	_run.call_deferred()

func _check(condition: bool, description: String) -> void:
	checks += 1
	if not condition:
		failures.append(description)
		push_error("FAIL " + description)

func _new_mission():
	rewards.clear()
	titles.clear()
	heat.clear()
	var mission = MISSIONS.new()
	root.add_child(mission)
	mission.completed.connect(func(title: String, reward: int) -> void:
		titles.append(title)
		rewards.append(reward))
	mission.heat_requested.connect(func(amount: float) -> void: heat.append(amount))
	return mission

func _run() -> void:
	_campaign()
	_race_retry_and_requirements()
	_race_exact_deadline()
	_restore_safe_stages()
	_restore_delivery_dwell()
	if failures.is_empty():
		print("MISSIONS_SMOKE_OK checks=", checks, " campaign=4/4 rewards=once race=timing+retry restore=safe")
		quit(0)
	else:
		print("MISSIONS_SMOKE_FAILED checks=", checks, " failures=", failures.size())
		for failure: String in failures:
			print("  ", failure)
		quit(1)

func _campaign() -> void:
	var mission = _new_mission()
	_check(mission.mission_index == 0 and mission.stage == 0 and not mission.finished, "campaign starts at first briefing")
	_check(not mission.interact(PACKAGE, false), "package cannot be picked up before entering the car")
	mission.tick(0.1, START, false, 0, 0.0)
	_check(mission.stage == 0, "walking near the starting car does not advance mission")
	mission.tick(0.1, START, true, 0, 0.0)
	_check(mission.stage == 1, "entering a car enables the first pickup")
	_check(not mission.interact(PACKAGE + Vector3(11.1,0,0), true), "package pickup rejects positions outside its radius")
	_check(mission.interact(PACKAGE + Vector3(11.0,0,0), true), "package pickup includes its exact proximity boundary")
	_check(not mission.interact(PACKAGE, true) and mission.stage == 2, "package can only be collected once")
	mission.tick(5.0, DELIVERY, true, 0, 3.0)
	_check(mission.mission_index == 0, "fast pass through delivery does not complete it")
	mission.tick(3.0, DELIVERY, false, 0, 0.0)
	_check(mission.mission_index == 0, "package delivery cannot be completed on foot")
	mission.tick(0.7, DELIVERY, true, 0, 0.0)
	_check(rewards.is_empty(), "delivery needs its full stopping dwell")
	mission.tick(0.1, DELIVERY + Vector3(12.1,0,0), true, 0, 0.0)
	mission.tick(0.7, DELIVERY, true, 0, 0.0)
	_check(rewards.is_empty(), "leaving delivery radius resets stopping dwell")
	mission.tick(0.51, DELIVERY, true, 0, 0.0)
	_check(mission.mission_index == 1 and rewards == [750], "first delivery rewards 750 and advances once")
	mission.tick(10.0, DELIVERY, true, 0, 0.0)
	_check(rewards.size() == 1, "remaining at old delivery cannot duplicate reward")

	_check(not mission.interact(LEDGER + Vector3(0,0,10.1), false), "ledger requires pickup proximity")
	_check(not mission.interact(LEDGER, true), "ledger requires leaving the car")
	_check(mission.interact(LEDGER, false), "ledger can be collected on foot")
	_check(heat == [48.0] and mission.stage == 1, "ledger triggers one level of pursuit heat")
	_check(not mission.interact(LEDGER, false) and heat.size() == 1, "repeated ledger interaction does not add heat")
	mission.tick(20.0, LEDGER, true, 0, 0.0)
	_check(mission.stage == 1 and not bool(mission.status()["has_target"]), "escape waits until pursuit has actually been observed")
	mission.tick(0.1, LEDGER, true, 2, 0.0)
	_check(mission.stage == 1, "active wanted stars keep escape objective active")
	mission.tick(0.1, LEDGER, true, 0, 0.0)
	_check(mission.stage == 2 and mission.status()["target"] == MARINA, "clearing an observed pursuit unlocks marina delivery")
	mission.tick(0.1, MARINA, true, 0, 9.0)
	_check(mission.mission_index == 1, "ledger delivery requires a slow or stopped vehicle")
	mission.tick(3.0, MARINA, false, 0, 0.0)
	_check(mission.mission_index == 1, "ledger delivery cannot be completed on foot")
	mission.tick(0.1, MARINA + Vector3(12.1,0,0), true, 0, 0.0)
	_check(mission.mission_index == 1, "ledger cannot be delivered outside destination radius")
	mission.tick(0.1, MARINA, true, 0, 0.0)
	_check(mission.mission_index == 2 and rewards == [750,1800], "ledger delivery rewards 1800 and advances once")

	_check(not mission.interact(RACE_START, false), "race cannot start on foot")
	_check(not mission.interact(RACE_START + Vector3(13.1,0,0), true), "race start requires proximity")
	_check(mission.interact(RACE_START, true), "race starts in a vehicle at its marker")
	_check(mission.checkpoint == 0 and is_zero_approx(mission.mission_time), "race begins with first checkpoint and a fresh clock")
	for index: int in range(MISSIONS.RACE.size()):
		mission.tick(10.0, MISSIONS.RACE[index], true, 0, 15.0)
		if index < MISSIONS.RACE.size()-1:
			_check(mission.checkpoint == index+1, "race advances exactly one ordered checkpoint")
	_check(mission.mission_index == 3 and rewards == [750,1800,2120], "60-second race pays base reward and exact time bonus")

	_check(not mission.interact(CARGO + Vector3(11.1,0,0), true), "final cargo requires pickup proximity")
	_check(not mission.interact(CARGO, false) and String(mission.status()["prompt"]).is_empty(), "final cargo requires a vehicle and hides pickup prompt on foot")
	_check(mission.interact(CARGO, true), "final cargo can be collected at the warehouse")
	_check(heat == [48.0,72.0], "final cargo requests its stronger pursuit once")
	mission.tick(10.0, CARGO, true, 0, 0.0)
	_check(mission.stage == 1, "final escape also requires an observed pursuit")
	mission.tick(0.1, CARGO, true, 3, 0.0)
	mission.tick(0.1, CARGO, true, 0, 0.0)
	_check(mission.stage == 2 and mission.status()["target"] == PARK, "final escape unlocks park destination")
	mission.tick(0.1, PARK, true, 0, 8.0)
	_check(not mission.finished, "final delivery requires stopping")
	mission.tick(3.0, PARK, false, 0, 0.0)
	_check(not mission.finished, "final delivery cannot be completed on foot")
	mission.tick(0.1, PARK, true, 0, 0.0)
	_check(mission.finished and mission.mission_index == 4, "fourth delivery finishes campaign")
	_check(rewards == [750,1800,2120,3500] and titles.size() == 4, "all four missions emit exactly one correct reward")
	for target: Vector3 in [PACKAGE,LEDGER,RACE_START,CARGO,PARK]:
		_check(not mission.interact(target, true), "finished campaign rejects further mission interactions")
		mission.tick(30.0,target,true,0,0.0)
	_check(rewards.size() == 4, "free roam cannot duplicate campaign rewards")
	var status: Dictionary = mission.status()
	_check(not bool(status["has_target"]) and is_equal_approx(float(status["progress"]),1.0), "finished campaign exposes free-roam HUD state")
	mission.free()
	print("PASS complete four-mission campaign and once-only rewards")

func _race_retry_and_requirements() -> void:
	var mission = _new_mission()
	mission.restore(2,0)
	mission.interact(RACE_START,true)
	mission.tick(1.0,MISSIONS.RACE[0],false,0,0.0)
	_check(mission.checkpoint == 0 and is_equal_approx(mission.mission_time,1.0), "race clock continues on foot but checkpoint does not count")
	mission.tick(1.0,MISSIONS.RACE[0],true,0,5.0)
	_check(mission.checkpoint == 1, "race checkpoint counts after reentering vehicle")
	mission.tick(1.0,MISSIONS.RACE[2],true,0,5.0)
	_check(mission.checkpoint == 1, "race rejects an out-of-order checkpoint")
	mission.tick(117.0,Vector3.ZERO,true,0,0.0)
	_check(mission.stage == 1 and is_equal_approx(mission.mission_time,120.0), "race remains live at exactly 120 seconds")
	mission.tick(0.01,Vector3.ZERO,true,0,0.0)
	_check(mission.stage == 0 and mission.checkpoint == 0 and mission.mission_index == 2, "race timeout resets route without skipping mission")
	_check(rewards.is_empty(), "race timeout neither pays nor deducts cash")
	_check(not mission.interact(Vector3.ZERO,true), "timed-out race must restart at its starting marker")
	_check(mission.interact(RACE_START,true) and is_zero_approx(mission.mission_time), "race retry starts with a fresh timer")
	for point: Vector3 in MISSIONS.RACE:
		mission.tick(10.0,point,true,0,12.0)
	_check(rewards == [2120] and mission.mission_index == 3, "successful retry awards the normal reward exactly once")
	mission.free()
	print("PASS race vehicle requirement, checkpoint order, timeout and retry")

func _race_exact_deadline() -> void:
	var mission = _new_mission()
	mission.restore(2,0)
	mission.interact(RACE_START,true)
	for point: Vector3 in MISSIONS.RACE:
		mission.tick(20.0,point,true,0,15.0)
	_check(mission.mission_index == 3 and rewards == [1400], "finishing exactly at race deadline pays base reward without bonus")
	mission.free()

func _restore_safe_stages() -> void:
	var mission = _new_mission()
	for index: int in [1,3]:
		mission.restore(index,1)
		_check(mission.mission_index == index and mission.stage == 0, "loading active pursuit restarts its pickup safely")
		mission.restore(index,2)
		_check(mission.stage == 2 and not mission.finished, "loading completed escape preserves unlocked delivery")
	mission.restore(2,1)
	_check(mission.stage == 0 and mission.checkpoint == 0 and is_zero_approx(mission.mission_time), "loading active race restarts at safe start marker")
	mission.restore(2,2)
	_check(mission.stage == 0, "any restored race stage normalizes to start")
	mission.restore(1,0)
	mission.interact(LEDGER,false)
	mission.tick(0.1,LEDGER,true,2,0.0)
	mission.restore(1,1)
	mission.interact(LEDGER,false)
	mission.tick(0.1,LEDGER,true,0,0.0)
	_check(mission.stage == 1, "restore clears prior pursuit-observed flag")
	mission.restore(-10,99)
	_check(mission.mission_index == 0 and mission.stage == 2, "restore clamps invalid index and stage")
	mission.restore(99,99)
	_check(mission.mission_index == 4 and mission.finished and not bool(mission.status()["has_target"]), "restore clamps completed campaign into safe free roam")
	mission.restore(0,0)
	_check(not mission.finished and mission.mission_index == 0 and mission.stage == 0, "restoring start allows a completed campaign to restart")
	_check(rewards.is_empty(), "restoring progress never grants a reward")
	mission.free()
	print("PASS safe save restore across pursuits, race and completed campaign")

func _restore_delivery_dwell() -> void:
	var mission = _new_mission()
	mission.restore(0,2)
	mission.tick(1.1,DELIVERY,true,0,0.0)
	mission.restore(0,2)
	mission.tick(0.2,DELIVERY,true,0,0.0)
	_check(mission.mission_index == 0 and rewards.is_empty(), "restore resets partial delivery dwell instead of inheriting it")
	mission.tick(1.01,DELIVERY,true,0,0.0)
	_check(mission.mission_index == 1 and rewards == [750], "restored delivery completes after a fresh full dwell")
	mission.free()

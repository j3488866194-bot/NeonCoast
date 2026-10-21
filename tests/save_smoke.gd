extends SceneTree

const STORE = preload("res://scripts/save_store.gd")
var failures: Array[String] = []
var save_name: String = ""
var settings_name: String = ""

func _initialize() -> void:
	_run.call_deferred()

func _check(condition: bool, message: String) -> void:
	if condition:
		print("PASS ", message)
	else:
		failures.append(message)
		push_error("FAIL " + message)

func _run() -> void:
	var suffix: String = str(OS.get_process_id()) + "-" + str(Time.get_ticks_usec())
	save_name = "test-profile-" + suffix + ".json"
	settings_name = "test-settings-" + suffix + ".json"
	_check(STORE.load_save(save_name).is_empty(), "missing save returns empty state")
	var payload: Dictionary = {
		"cash": 5200, "mission_index": 2, "mission_stage": 1,
		"position": Vector3(22.5, 0.2, -40.0), "car_position": [20.0, 0.1, -42.0],
		"car_yaw": 1.5, "health": 72.0, "time_of_day": 18.75,
		"collectibles": [1, 4, "pier-hidden"],
	}
	_check(STORE.write_save(payload, save_name), "first profile write succeeds")
	var loaded: Dictionary = STORE.load_save(save_name)
	_check(loaded.get("cash") == 5200 and loaded.get("mission_index") == 2 and loaded.get("health") == 72.0, "progress roundtrip preserves numeric state")
	var loaded_position: Array = loaded.get("position", [])
	_check(loaded_position.size() == 3 and is_equal_approx(float(loaded_position[0]), 22.5) and is_equal_approx(float(loaded_position[1]), 0.2) and is_equal_approx(float(loaded_position[2]), -40.0) and loaded.get("car_yaw") == 1.5, "position Vector3 serializes to a safe numeric array")
	_check(loaded.get("collectibles") == [1, 4, "pier-hidden"], "collectible IDs roundtrip")
	payload["cash"] = 6000
	_check(STORE.write_save(payload, save_name), "replacement write succeeds")
	_check(FileAccess.file_exists("user://" + save_name + ".bak"), "replacement retains previous profile backup")
	_check(not FileAccess.file_exists("user://" + save_name + ".tmp"), "successful write leaves no temporary file")
	DirAccess.remove_absolute(ProjectSettings.globalize_path("user://" + save_name))
	var recovered: Dictionary = STORE.load_save(save_name)
	_check(recovered.get("cash") == 5200 and not FileAccess.file_exists("user://" + save_name), "missing active file recovers previous backup without mutating files")
	_check(STORE.write_save(payload, save_name), "profile can be restored after an interrupted write")
	var invalid: Dictionary = {
		"cash": -15, "mission_index": 9999, "mission_stage": -3,
		"position": [99999.0, -99.0, -99999.0], "car_position": ["wrong", INF, 0.0],
		"car_yaw": NAN, "health": 3000.0, "time_of_day": -4,
		"collectibles": [1, 1, -4, {"nested": true}, "", "valid", "valid"],
	}
	_check(STORE.write_save(invalid, save_name), "malformed values sanitize before serialization")
	loaded = STORE.load_save(save_name)
	_check(loaded.get("cash") == 0 and loaded.get("mission_index") == 64 and loaded.get("mission_stage") == 0, "cash and mission bounds clamp")
	_check(loaded.get("position") == [308.0, 0.0, -246.0] and loaded.get("car_position") == [0.0, 0.1, 0.0], "unsafe positions and nonfinite coordinates sanitize")
	_check(loaded.get("health") == 100.0 and loaded.get("car_yaw") == 0.0 and loaded.get("time_of_day") == 0.0, "health time and nonfinite angles sanitize")
	_check(loaded.get("collectibles") == [1, "valid"], "collectibles reject invalid entries and duplicates")
	var broken: FileAccess = FileAccess.open("user://" + save_name, FileAccess.WRITE)
	broken.store_string("{ this is deliberately broken JSON")
	broken.close()
	_check(STORE.load_save(save_name).is_empty(), "corrupt JSON returns empty state without throwing")
	_check(FileAccess.get_file_as_string("user://" + save_name) == "{ this is deliberately broken JSON", "read preserves original corrupt bytes")
	_check(STORE.write_save(payload, save_name), "explicit write can replace a corrupt save")
	var archives: Array[String] = []
	for filename: String in DirAccess.get_files_at("user://"):
		if filename.begins_with(save_name + ".corrupt."):
			archives.append(filename)
	_check(archives.size() == 1 and FileAccess.get_file_as_string("user://" + archives[0]) == "{ this is deliberately broken JSON", "corrupt original is archived before explicit replacement")
	_check(STORE.write_settings({"volume": 5.0, "sensitivity": -2.0, "quality": "ultra", "invert_y": "yes"}, settings_name), "settings write is isolated from profile")
	var settings: Dictionary = STORE.load_settings(settings_name)
	_check(settings.get("volume") == 1.0 and settings.get("sensitivity") == 0.1 and settings.get("quality") == "high" and settings.get("invert_y") == false, "settings ranges and types sanitize")
	_check(not STORE.write_save(payload, "../escape.json") and not STORE.write_save(payload, "C:\\escape.json"), "filenames cannot escape the user save directory")
	_check(STORE.clear_save(save_name) and STORE.load_save(save_name).is_empty(), "clear removes active save and rollback files")
	STORE.clear_save(settings_name)
	for archive: String in archives:
		DirAccess.remove_absolute(ProjectSettings.globalize_path("user://" + archive))
	print("SAVE_SMOKE_COMPLETE failures=", failures.size())
	quit(0 if failures.is_empty() else 1)

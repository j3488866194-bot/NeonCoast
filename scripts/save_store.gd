extends RefCounted
## Validated local profiles. Reads never modify or remove a player's file.
## Optional filenames support isolated tests without touching real progress.

const SCHEMA_VERSION: int = 1
const MAX_FILE_BYTES: int = 1024 * 1024
const MAX_CASH: int = 99999999

static func load_save(filename: String = "profile.json") -> Dictionary:
	var path: String = _safe_path(filename)
	if path.is_empty():
		return {}
	var record: Dictionary = _read_record(path)
	if not bool(record.get("exists", false)):
		# A power interruption between the two rename operations can leave only
		# the previous file. Recover it in memory without changing either file.
		record = _read_record(path + ".bak")
	if not bool(record.get("valid", false)):
		return {}
	return _sanitize_save(record["value"])

static func write_save(data: Dictionary, filename: String = "profile.json") -> bool:
	return _atomic_write(_sanitize_save(data), filename)

static func clear_save(filename: String = "profile.json") -> bool:
	var path: String = _safe_path(filename)
	if path.is_empty():
		return false
	var success: bool = true
	for suffix: String in ["", ".tmp", ".bak"]:
		if not _remove_if_exists(path + suffix):
			success = false
	return success

static func load_settings(filename: String = "settings.json") -> Dictionary:
	var path: String = _safe_path(filename)
	if path.is_empty():
		return {}
	var record: Dictionary = _read_record(path)
	if not bool(record.get("exists", false)):
		record = _read_record(path + ".bak")
	if not bool(record.get("valid", false)):
		return {}
	return _sanitize_settings(record["value"])

static func write_settings(data: Dictionary, filename: String = "settings.json") -> bool:
	return _atomic_write(_sanitize_settings(data), filename)

static func _safe_path(filename: String) -> String:
	# Only a single leaf name is permitted, always inside the Godot user folder.
	if filename.is_empty() or filename.length() > 120:
		return ""
	if filename.contains("/") or filename.contains("\\") or filename.contains(":") or filename.contains(".."):
		return ""
	if filename.begins_with(".") or not filename.ends_with(".json"):
		return ""
	for invalid: String in ["<", ">", "\"", "|", "?", "*", "\n", "\r"]:
		if filename.contains(invalid):
			return ""
	return "user://" + filename

static func _number(value: Variant, fallback: float, minimum: float, maximum: float) -> float:
	if typeof(value) != TYPE_INT and typeof(value) != TYPE_FLOAT:
		return fallback
	var result: float = float(value)
	if not is_finite(result):
		return fallback
	return clampf(result, minimum, maximum)

static func _position(value: Variant, fallback: Array = [0.0, 0.1, 0.0]) -> Array:
	var coordinates: Array = []
	if typeof(value) == TYPE_VECTOR3:
		var vector: Vector3 = value
		coordinates = [vector.x, vector.y, vector.z]
	elif typeof(value) == TYPE_ARRAY and value.size() == 3:
		coordinates = value
	else:
		return fallback.duplicate()
	return [
		_number(coordinates[0], float(fallback[0]), -246.0, 308.0),
		_number(coordinates[1], float(fallback[1]), 0.0, 80.0),
		_number(coordinates[2], float(fallback[2]), -246.0, 246.0)
	]

static func _sanitize_save(data: Dictionary) -> Dictionary:
	var collected: Array = []
	var raw_collected: Variant = data.get("collectibles", [])
	if typeof(raw_collected) == TYPE_ARRAY:
		for item: Variant in raw_collected:
			if collected.size() >= 256:
				break
			# IDs can be integers or short stable names. Reject nested/unbounded data.
			if typeof(item) == TYPE_INT or typeof(item) == TYPE_FLOAT:
				var identifier: float = _number(item, -1.0, -1.0, 4096.0)
				if identifier >= 0.0 and is_equal_approx(identifier, floorf(identifier)):
					var integer_id: int = int(identifier)
					if not collected.has(integer_id):
						collected.append(integer_id)
			elif typeof(item) == TYPE_STRING:
				var string_id: String = str(item).strip_edges()
				if not string_id.is_empty() and string_id.length() <= 64 and not collected.has(string_id):
					collected.append(string_id)
	return {
		"schema_version": SCHEMA_VERSION,
		"cash": int(_number(data.get("cash", 0), 0.0, 0.0, float(MAX_CASH))),
		"mission_index": int(_number(data.get("mission_index", 0), 0.0, 0.0, 64.0)),
		"mission_stage": int(_number(data.get("mission_stage", 0), 0.0, 0.0, 32.0)),
		"position": _position(data.get("position", [])),
		"car_position": _position(data.get("car_position", [])),
		"car_yaw": _number(data.get("car_yaw", 0.0), 0.0, -TAU * 100.0, TAU * 100.0),
		"health": _number(data.get("health", 100.0), 100.0, 0.0, 100.0),
		"time_of_day": _number(data.get("time_of_day", 17.5), 17.5, 0.0, 24.0),
		"collectibles": collected,
	}

static func _sanitize_settings(data: Dictionary) -> Dictionary:
	var quality: Variant = data.get("quality", "high")
	var quality_name: String = "high"
	if typeof(quality) == TYPE_STRING and str(quality) in ["low", "medium", "high"]:
		quality_name = str(quality)
	elif typeof(quality) == TYPE_INT or typeof(quality) == TYPE_FLOAT:
		var quality_index: int = int(_number(quality, 2.0, 0.0, 2.0))
		quality_name = ["low", "medium", "high"][quality_index]
	var invert: Variant = data.get("invert_y", false)
	return {
		"schema_version": SCHEMA_VERSION,
		"volume": _number(data.get("volume", 0.75), 0.75, 0.0, 1.0),
		"sensitivity": _number(data.get("sensitivity", 1.0), 1.0, 0.1, 3.0),
		"quality": quality_name,
		"invert_y": invert if typeof(invert) == TYPE_BOOL else false,
	}

static func _read_record(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {"exists": false, "valid": false, "value": {}}
	var file: FileAccess = FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {"exists": true, "valid": false, "value": {}}
	if file.get_length() > MAX_FILE_BYTES:
		file.close()
		return {"exists": true, "valid": false, "value": {}}
	var contents: String = file.get_as_text()
	file.close()
	var parser: JSON = JSON.new()
	if parser.parse(contents) != OK:
		return {"exists": true, "valid": false, "value": {}}
	var parsed: Variant = parser.data
	if typeof(parsed) != TYPE_DICTIONARY:
		return {"exists": true, "valid": false, "value": {}}
	var version: Variant = parsed.get("schema_version", null)
	if (typeof(version) != TYPE_INT and typeof(version) != TYPE_FLOAT) or float(version) != float(SCHEMA_VERSION):
		return {"exists": true, "valid": false, "value": {}}
	return {"exists": true, "valid": true, "value": parsed}

static func _remove_if_exists(path: String) -> bool:
	if not FileAccess.file_exists(path):
		return true
	return DirAccess.remove_absolute(ProjectSettings.globalize_path(path)) == OK

static func _rename(source: String, destination: String) -> bool:
	return DirAccess.rename_absolute(ProjectSettings.globalize_path(source), ProjectSettings.globalize_path(destination)) == OK

static func _atomic_write(data: Dictionary, filename: String) -> bool:
	var path: String = _safe_path(filename)
	if path.is_empty():
		return false
	var temporary_path: String = path + ".tmp"
	var file: FileAccess = FileAccess.open(temporary_path, FileAccess.WRITE)
	if file == null:
		return false
	file.store_string(JSON.stringify(data, "\t") + "\n")
	file.flush()
	var file_error: Error = file.get_error()
	file.close()
	if file_error != OK or not bool(_read_record(temporary_path).get("valid", false)):
		_remove_if_exists(temporary_path)
		return false
	var old_record: Dictionary = _read_record(path)
	var rollback_path: String = ""
	if bool(old_record.get("exists", false)):
		if bool(old_record.get("valid", false)):
			rollback_path = path + ".bak"
			if not _remove_if_exists(rollback_path):
				_remove_if_exists(temporary_path)
				return false
		else:
			# Preserve unreadable/unsupported data only after an explicit write request.
			rollback_path = path + ".corrupt." + str(int(Time.get_unix_time_from_system())) + "-" + str(Time.get_ticks_usec())
		if not _rename(path, rollback_path):
			_remove_if_exists(temporary_path)
			return false
	if not _rename(temporary_path, path):
		if not rollback_path.is_empty():
			_rename(rollback_path, path)
		_remove_if_exists(temporary_path)
		return false
	return true

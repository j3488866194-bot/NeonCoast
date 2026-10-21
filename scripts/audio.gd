extends Node
## Original procedural soundtrack and compact cached PCM effects. No external assets.
## All synthesis happens once on startup; _process only mixes cached audio players.

const SAMPLE_RATE: int = 22050
const BUS_NAME: String = "NeonCoast"
const MUSIC_LENGTH: float = 8.0

var speed: float = 0.0
var wanted: int = 0
var in_vehicle: bool = false
var enabled: bool = true

var _started: bool = false
var _station: int = 0
var _master_volume: float = 0.65
var _music: AudioStreamPlayer
var _engine: AudioStreamPlayer
var _ambience: AudioStreamPlayer
var _siren: AudioStreamPlayer
var _effect_players: Array[AudioStreamPlayer] = []
var _effect_cursor: int = 0
var _music_streams: Array[AudioStreamWAV] = []
var _effects: Dictionary = {}
var _bus_index: int = -1
var _last_enabled: bool = true
var _engine_gain: float = 0.0
var _siren_gain: float = 0.0
var _music_gain: float = 0.0


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_PAUSABLE
	start()


func _exit_tree() -> void:
	# Release playback references before the audio server shuts down. Looping
	# players otherwise keep their WAV/playback resources alive during fast exits.
	for child: Node in get_children():
		if child is AudioStreamPlayer:
			child.stop()
			child.stream = null
	_music_streams.clear()
	_effects.clear()
	_effect_players.clear()
	_started = false


func start() -> void:
	if _started:
		return
	_started = true
	_bus_index = AudioServer.get_bus_index(BUS_NAME)
	if _bus_index < 0:
		AudioServer.add_bus()
		_bus_index = AudioServer.bus_count - 1
		AudioServer.set_bus_name(_bus_index, BUS_NAME)
		AudioServer.set_bus_send(_bus_index, "Master")
	set_master_volume(_master_volume)
	_music_streams.append(_make_music(0))
	_music_streams.append(_make_music(1))
	_music = _new_player("Radio", -28.0)
	_music.stream = _music_streams[0]
	_music.play()
	_engine = _new_player("Engine", -60.0)
	_engine.stream = _make_loop("engine", 1.0)
	_engine.play()
	_ambience = _new_player("CoastalAmbience", -28.0)
	_ambience.stream = _make_loop("ambience", 4.0)
	_ambience.play()
	_siren = _new_player("PoliceSiren", -60.0)
	_siren.stream = _make_loop("siren", 2.0)
	_siren.play()
	for kind: String in ["crash", "shot", "confirm", "mission", "horn"]:
		_effects[kind] = _make_effect(kind)
	for index: int in range(6):
		_effect_players.append(_new_player("Effect%d" % index, -10.0))


func _new_player(player_name: String, decibels: float) -> AudioStreamPlayer:
	var player: AudioStreamPlayer = AudioStreamPlayer.new()
	player.name = player_name
	player.bus = BUS_NAME
	player.volume_db = decibels
	add_child(player)
	return player


func set_master_volume(value: float) -> void:
	_master_volume = clampf(value, 0.0, 1.0)
	if _bus_index >= 0:
		AudioServer.set_bus_volume_db(_bus_index, linear_to_db(maxf(0.0001, _master_volume)))
		AudioServer.set_bus_mute(_bus_index, _master_volume <= 0.0 or not enabled)


func set_radio(station: int) -> void:
	_station = posmod(station, 3)
	if not _started or _music == null:
		return
	if _station < 2 and _music.stream != _music_streams[_station]:
		_music.stream = _music_streams[_station]
		_music.play()


func play_effect(kind: String) -> void:
	if not enabled or not _effects.has(kind) or _effect_players.is_empty():
		return
	var player: AudioStreamPlayer = _effect_players[_effect_cursor]
	_effect_cursor = (_effect_cursor + 1) % _effect_players.size()
	player.stream = _effects[kind]
	player.volume_db = -14.0 if kind == "shot" else (-10.0 if kind == "crash" else -12.0)
	player.pitch_scale = 1.0
	player.play()


func _process(delta: float) -> void:
	if not _started or _engine == null:
		return
	if enabled != _last_enabled:
		_last_enabled = enabled
		set_master_volume(_master_volume)
	var acceleration: float = clampf(absf(speed) / 48.0, 0.0, 1.0)
	var blend: float = 1.0 - exp(-delta * 5.0)
	_engine_gain = lerpf(_engine_gain, (0.17 + acceleration * 0.31) if in_vehicle else 0.0, blend)
	_siren_gain = lerpf(_siren_gain, minf(0.29, float(wanted) * 0.065), blend)
	_music_gain = lerpf(_music_gain, (0.42 if in_vehicle else 0.28) if _station < 2 else 0.0, blend)
	_engine.volume_db = linear_to_db(maxf(0.0001, _engine_gain))
	_engine.pitch_scale = lerpf(_engine.pitch_scale, 0.72 + acceleration * 1.55, blend)
	_siren.volume_db = linear_to_db(maxf(0.0001, _siren_gain))
	_music.volume_db = linear_to_db(maxf(0.0001, _music_gain))
	_ambience.volume_db = -33.0 if in_vehicle else -27.0


func _make_music(station: int) -> AudioStreamWAV:
	var count: int = int(MUSIC_LENGTH * SAMPLE_RATE)
	var pcm: PackedByteArray = PackedByteArray()
	pcm.resize(count * 2)
	# Four original two-second bars; all pitches snap to loop periods for clean pads.
	var roots: PackedInt32Array = PackedInt32Array([45, 41, 48, 43] if station == 0 else [50, 46, 53, 48])
	var melody: PackedInt32Array = PackedInt32Array([12, 19, 15, 22, 19, 15, 10, 14] if station == 0 else [12, 7, 10, 14, 17, 14, 10, 7])
	var rng: RandomNumberGenerator = RandomNumberGenerator.new()
	rng.seed = 72043 + station
	var filtered_noise: float = 0.0
	for index: int in range(count):
		var time: float = float(index) / SAMPLE_RATE
		var bar: int = mini(3, int(time / 2.0))
		var bar_time: float = fmod(time, 2.0)
		var beat_time: float = fmod(time, 0.5)
		var step_time: float = fmod(time, 0.25)
		var root: float = _note(roots[bar])
		var chord_envelope: float = minf(1.0, bar_time / 0.12) * minf(1.0, (2.0 - bar_time) / 0.20)
		var third: float = root * (pow(2.0, 3.0 / 12.0) if bar == 0 else pow(2.0, 4.0 / 12.0))
		var pad: float = (sin(TAU * root * 2.0 * time) + sin(TAU * third * 2.0 * time) + sin(TAU * root * 3.0 * time)) * 0.027 * chord_envelope
		var bass_envelope: float = (1.0 - exp(-beat_time * 90.0)) * exp(-beat_time * 7.0)
		var bass: float = (sin(TAU * root * time) + sin(TAU * root * 2.0 * time) * 0.16) * bass_envelope * 0.16
		var melody_index: int = int(time / 0.25) % melody.size()
		var lead_pitch: float = _note(roots[bar] + melody[melody_index] + 12)
		var lead_envelope: float = (1.0 - exp(-step_time * 140.0)) * exp(-step_time * 14.0)
		var lead: float = sin(TAU * lead_pitch * time + sin(TAU * lead_pitch * 2.0 * time) * 0.25) * lead_envelope * 0.08
		var kick: float = sin(TAU * (48.0 * beat_time + 7.0 * (1.0 - exp(-beat_time * 22.0)))) * exp(-beat_time * 24.0) * 0.19
		filtered_noise = lerpf(filtered_noise, rng.randf_range(-1.0, 1.0), 0.38)
		var snare_time: float = fmod(time + 0.5, 1.0)
		var snare: float = filtered_noise * exp(-snare_time * 35.0) * 0.11
		var hat: float = filtered_noise * exp(-step_time * 90.0) * 0.035
		var fade: float = minf(1.0, time / 0.008) * minf(1.0, (MUSIC_LENGTH - time) / 0.008)
		_encode_sample(pcm, index, (pad + bass + lead + kick + snare + hat) * fade)
	return _wav(pcm, true)


func _make_loop(kind: String, length: float) -> AudioStreamWAV:
	var count: int = int(length * SAMPLE_RATE)
	var pcm: PackedByteArray = PackedByteArray()
	pcm.resize(count * 2)
	var rng: RandomNumberGenerator = RandomNumberGenerator.new()
	rng.seed = 98631
	var noise: float = 0.0
	for index: int in range(count):
		var time: float = float(index) / SAMPLE_RATE
		var sample: float = 0.0
		if kind == "engine":
			# Integer-frequency partials create a phase-continuous one-second loop.
			var pulse: float = sin(TAU * 52.0 * time)
			sample = pulse * 0.29 + sin(TAU * 104.0 * time) * 0.13 + sin(TAU * 156.0 * time) * 0.05
			sample *= 0.82 + 0.18 * sin(TAU * 13.0 * time)
		elif kind == "siren":
			# Smooth frequency modulation, 540–820 Hz, with continuous loop phase.
			var phase: float = TAU * 680.0 * time - 280.0 * cos(PI * time)
			sample = (sin(phase) * 0.29 + sin(phase * 2.0) * 0.035) * (0.85 + 0.15 * sin(PI * time))
		else:
			noise = lerpf(noise, rng.randf_range(-1.0, 1.0), 0.055)
			var wash: float = 0.55 + 0.25 * sin(TAU * time / length)
			var fade: float = minf(1.0, time / 0.08) * minf(1.0, (length - time) / 0.08)
			sample = (noise * wash * 0.6 + sin(TAU * 82.0 * time) * 0.015) * fade
		_encode_sample(pcm, index, sample)
	return _wav(pcm, true)


func _make_effect(kind: String) -> AudioStreamWAV:
	var length: float = 0.22
	match kind:
		"crash": length = 0.48
		"shot": length = 0.19
		"confirm": length = 0.24
		"mission": length = 1.05
		"horn": length = 0.40
	var count: int = int(length * SAMPLE_RATE)
	var pcm: PackedByteArray = PackedByteArray()
	pcm.resize(count * 2)
	var rng: RandomNumberGenerator = RandomNumberGenerator.new()
	rng.seed = 711
	var noise: float = 0.0
	for index: int in range(count):
		var time: float = float(index) / SAMPLE_RATE
		var sample: float = 0.0
		noise = lerpf(noise, rng.randf_range(-1.0, 1.0), 0.25)
		match kind:
			"crash":
				sample = (noise * 0.75 + sin(TAU * (55.0 * time + 3.0 * (1.0 - exp(-time * 20.0)))) * 0.27) * exp(-time * 12.0)
			"shot":
				sample = (noise * 0.7 + sin(TAU * 90.0 * time) * 0.3) * exp(-time * 34.0)
			"confirm":
				sample = sin(TAU * 880.0 * time) * exp(-time * 17.0) * 0.24
			"mission":
				var notes: Array[int] = [69, 72, 76, 81]
				for note_index: int in range(notes.size()):
					var onset: float = time - float(note_index) * 0.13
					if onset >= 0.0:
						sample += sin(TAU * _note(notes[note_index]) * onset) * (1.0 - exp(-onset * 150.0)) * exp(-onset * 5.0) * 0.15
			"horn":
				sample = (sin(TAU * 349.23 * time) + sin(TAU * 440.0 * time)) * 0.16
		var fade: float = minf(1.0, time / 0.004) * minf(1.0, (length - time) / 0.04)
		_encode_sample(pcm, index, sample * fade)
	return _wav(pcm, false)


func _note(midi: int) -> float:
	return 440.0 * pow(2.0, float(midi - 69) / 12.0)


func _encode_sample(bytes: PackedByteArray, index: int, sample: float) -> void:
	bytes.encode_s16(index * 2, clampi(int(sample * 32760.0), -32760, 32760))


func _wav(bytes: PackedByteArray, loop: bool) -> AudioStreamWAV:
	var stream: AudioStreamWAV = AudioStreamWAV.new()
	stream.format = AudioStreamWAV.FORMAT_16_BITS
	stream.mix_rate = SAMPLE_RATE
	stream.stereo = false
	stream.data = bytes
	if loop:
		stream.loop_mode = AudioStreamWAV.LOOP_FORWARD
		stream.loop_begin = 0
		stream.loop_end = bytes.size() / 2
	return stream

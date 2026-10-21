extends Control

signal waypoint_selected(world_position: Vector3)

const CREAM := Color("fff2d6")
const TEAL := Color("6ee6ca")
const CORAL := Color("ff755f")
const DUSK := Color("101d28")
const GOLD := Color("ffc95c")
const WATER := Color("173547")
const LAND := Color("46554a")
const SAND := Color("cdb489")
const ROAD := Color("252c33")
const ROAD_EDGE := Color("7d8a80")
const VIEW_MIN := Vector2(-260.0, -260.0)
const VIEW_MAX := Vector2(330.0, 260.0)
const SAFE := 246.0
const CURSOR_SPEED := 120.0
const DISTRICTS := [["NORTH HILLS", Vector2(-108, -180)], ["HARBOR ROW", Vector2(144, -144)], ["MIDTOWN", Vector2(-36, 36)], ["WESTSIDE", Vector2(-180, 108)], ["SOUTH DOCKS", Vector2(108, 180)], ["BEACH", Vector2(280, -100)]]

var full_map: bool = false: set = _set_full_map
var blocks: Array = []
var state: Dictionary = {}
var cursor_world: Vector2 = Vector2.ZERO
var _cursor_ready: bool = false
var _scale: float = 1.0
var _center: Vector2 = Vector2.ZERO

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	clip_contents = true
	focus_entered.connect(queue_redraw)
	focus_exited.connect(queue_redraw)
	resized.connect(queue_redraw)
	_apply_mode()

func _set_full_map(value: bool) -> void:
	full_map = value
	if is_inside_tree():
		_apply_mode()

func _apply_mode() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP if full_map else Control.MOUSE_FILTER_IGNORE
	focus_mode = Control.FOCUS_ALL if full_map else Control.FOCUS_NONE
	custom_minimum_size = Vector2(820, 690) if full_map else Vector2(250, 205)
	queue_redraw()

func set_data(data: Dictionary, new_blocks: Array) -> void:
	state = data
	blocks = new_blocks
	if not _cursor_ready:
		cursor_world = _get_v2("player_position").clamp(Vector2(-SAFE, -SAFE), Vector2(SAFE, SAFE))
		_cursor_ready = true
	queue_redraw()

func _process(delta: float) -> void:
	if not full_map or not has_focus():
		return
	var axis := Vector2(Input.get_action_strength("ui_right") - Input.get_action_strength("ui_left"), Input.get_action_strength("ui_down") - Input.get_action_strength("ui_up"))
	if axis.length_squared() > 0.0001:
		cursor_world = (cursor_world + axis.limit_length(1.0) * CURSOR_SPEED * delta).clamp(Vector2(-SAFE, -SAFE), Vector2(SAFE, SAFE))
		queue_redraw()

func _gui_input(event: InputEvent) -> void:
	if not full_map:
		return
	if event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		if mb.pressed and mb.button_index == MOUSE_BUTTON_LEFT:
			grab_focus()
			_update_view()
			cursor_world = _to_world(mb.position).clamp(Vector2(-SAFE, -SAFE), Vector2(SAFE, SAFE))
			_emit_cursor()
			accept_event()
	elif has_focus():
		if event.is_action_pressed("ui_accept"):
			_emit_cursor()
			accept_event()
		elif event.is_action("ui_left") or event.is_action("ui_right") or event.is_action("ui_up") or event.is_action("ui_down"):
			accept_event()

func _emit_cursor() -> void:
	waypoint_selected.emit(Vector3(cursor_world.x, 0.0, cursor_world.y))
	queue_redraw()

func _get_v2(key: String) -> Vector2:
	var v: Variant = state.get(key, Vector3.ZERO)
	if v is Vector3:
		var v3: Vector3 = v
		return Vector2(v3.x, v3.z)
	return Vector2.ZERO

func _update_view() -> void:
	if full_map:
		var span := VIEW_MAX - VIEW_MIN
		_scale = maxf(minf(size.x / span.x, size.y / span.y), 0.01)
		_center = (VIEW_MIN + VIEW_MAX) * 0.5
	else:
		_scale = maxf(0.9 * size.x / 250.0, 0.01)
		_center = _get_v2("player_position")

func _to_map(w: Vector2) -> Vector2:
	return (w - _center) * _scale + size * 0.5

func _to_world(p: Vector2) -> Vector2:
	return (p - size * 0.5) / _scale + _center

func _wrect(a: Vector2, b: Vector2) -> Rect2:
	return Rect2(_to_map(a), (b - a) * _scale)

func _draw() -> void:
	_update_view()
	var full := Rect2(Vector2.ZERO, size)
	draw_rect(full, WATER)
	draw_rect(_wrect(Vector2(-255, -255), Vector2(250, 255)), LAND)
	draw_rect(_wrect(Vector2(250, -255), Vector2(310, 255)), SAND)
	draw_rect(_wrect(Vector2(306, -255), Vector2(310, 255)), Color(CREAM, 0.35))
	for item in blocks:
		if item is Dictionary:
			_draw_block(item)
	_draw_roads()
	_draw_garage()
	var font: Font = get_theme_default_font()
	if font == null:
		font = ThemeDB.fallback_font
	if full_map:
		var fs: int = maxi(10, int(11.0 * _scale))
		for entry in DISTRICTS:
			var title: String = entry[0]
			var at: Vector2 = entry[1]
			_label(font, _to_map(at), title, fs, Color(CREAM, 0.55))
	var player := _get_v2("player_position")
	var has_obj: bool = bool(state.get("mission_active", state.get("has_objective", false)))
	var has_wp: bool = bool(state.get("waypoint_active", false))
	var obj := _get_v2("mission_position") if state.has("mission_position") else _get_v2("objective_position")
	var wp := _get_v2("waypoint_position")
	if has_wp:
		_draw_route(player, wp, TEAL)
	if has_obj:
		_draw_route(player, obj, GOLD)
	if has_wp:
		_draw_marker(wp, TEAL, true)
	if has_obj:
		_draw_marker(obj, GOLD, false)
	_draw_player(player)
	if full_map and has_focus():
		var cp := _to_map(cursor_world)
		for d: Vector2 in [Vector2.LEFT, Vector2.RIGHT, Vector2.UP, Vector2.DOWN]:
			draw_line(cp + d * 5.0, cp + d * 13.0, TEAL, 2.0, true)
		draw_arc(cp, 6.0, 0.0, TAU, 20, TEAL, 1.5, true)
		draw_rect(full.grow(-1.5), TEAL, false, 3.0)
	else:
		var wanted: int = int(state.get("wanted", 0))
		draw_rect(full.grow(-1.0), CORAL if wanted > 0 else Color(CREAM, 0.35), false, 2.0)
	var np := Vector2(size.x - 18.0, 18.0)
	draw_circle(np, 11.0, Color(DUSK, 0.85))
	draw_arc(np, 11.0, 0.0, TAU, 24, Color(CREAM, 0.6), 1.2, true)
	_label(font, np, "N", 13, CREAM)
	if full_map:
		var hint := "A  Set waypoint" if bool(state.get("controller", false)) else "Click / Enter  Set waypoint"
		draw_string(font, Vector2(14.0, size.y - 14.0), hint, HORIZONTAL_ALIGNMENT_LEFT, -1, 13, Color(CREAM, 0.7))

func _label(font: Font, p: Vector2, text: String, fs: int, col: Color) -> void:
	draw_string(font, Vector2(p.x - 80.0, p.y + fs * 0.35), text, HORIZONTAL_ALIGNMENT_CENTER, 160.0, fs, col)

func _draw_block(b: Dictionary) -> void:
	var x: float = float(b.get("x", 0.0))
	var z: float = float(b.get("z", 0.0))
	var w: float = float(b.get("w", 20.0))
	var d: float = float(b.get("d", 20.0))
	var raw: Variant = b.get("color", LAND)
	var c: Color = Color(0.5, 0.52, 0.5)
	if raw is Color:
		c = raw
	var tint: float = float(posmod(int(x * 3.0 + z * 7.0), 9)) * 0.012
	c = c.lerp(Color("5d6863"), 0.6).lightened(tint)
	var r := _wrect(Vector2(x - w * 0.5, z - d * 0.5), Vector2(x + w * 0.5, z + d * 0.5))
	draw_rect(r, c)
	draw_rect(r, Color(DUSK, 0.35), false, 1.0)

func _draw_roads() -> void:
	for layer in 3:
		for i in range(-3, 4):
			var k: float = i * 72.0
			var segs: Array[Vector2] = [_to_map(Vector2(k, -255)), _to_map(Vector2(k, 255)), _to_map(Vector2(-255, k)), _to_map(Vector2(250, k))]
			for s in [0, 2]:
				if layer == 0:
					draw_line(segs[s], segs[s + 1], ROAD_EDGE, 16.0 * _scale + 2.0)
				elif layer == 1:
					draw_line(segs[s], segs[s + 1], ROAD, 16.0 * _scale)
				else:
					draw_dashed_line(segs[s], segs[s + 1], Color(CREAM, 0.22), 1.0, 5.0)

func _draw_garage() -> void:
	var p := _to_map(Vector2(216, 0))
	draw_circle(p, 7.0, DUSK)
	draw_arc(p, 7.0, 0.0, TAU, 20, CORAL, 1.5, true)
	draw_colored_polygon(PackedVector2Array([p + Vector2(-4.5, -1), p + Vector2(0, -5), p + Vector2(4.5, -1)]), CREAM)
	draw_rect(Rect2(p + Vector2(-3.5, -1), Vector2(7, 4.5)), CREAM)
	draw_rect(Rect2(p + Vector2(-1.5, 0.5), Vector2(3, 3)), CORAL)

func _draw_route(from: Vector2, to: Vector2, col: Color) -> void:
	var rx := clampf(snappedf(from.x, 72.0), -216.0, 216.0)
	var rz := clampf(snappedf(from.y, 72.0), -216.0, 216.0)
	var tx := clampf(snappedf(to.x, 72.0), -216.0, 216.0)
	var tz := clampf(snappedf(to.y, 72.0), -216.0, 216.0)
	var stops: Array[Vector2] = [from, Vector2(rx, from.y), Vector2(rx, tz), Vector2(to.x, tz), to]
	if absf(from.x - rx) > absf(from.y - rz):
		stops = [from, Vector2(from.x, rz), Vector2(tx, rz), Vector2(tx, to.y), to]
	var pts := PackedVector2Array()
	for w: Vector2 in stops:
		pts.append(_to_map(w))
	draw_polyline(pts, Color(DUSK, 0.8), 5.0, true)
	draw_polyline(pts, Color(col, 0.85), 2.5, true)

func _draw_marker(world: Vector2, col: Color, diamond: bool) -> void:
	var p := _to_map(world)
	var inner := Rect2(Vector2(14, 14), size - Vector2(28, 28))
	if inner.has_point(p):
		draw_arc(p, 10.0, 0.0, TAU, 24, Color(col, 0.45), 1.5, true)
		if diamond:
			var dm := PackedVector2Array([p + Vector2(0, -7), p + Vector2(7, 0), p + Vector2(0, 7), p + Vector2(-7, 0)])
			draw_colored_polygon(dm, col)
			dm.append(dm[0])
			draw_polyline(dm, DUSK, 1.5, true)
		else:
			draw_circle(p, 7.0, DUSK)
			draw_circle(p, 5.5, col)
		return
	var c := size * 0.5
	var d := p - c
	if d.length_squared() < 0.01:
		return
	var half := inner.size * 0.5
	var t := minf(half.x / maxf(absf(d.x), 0.001), half.y / maxf(absf(d.y), 0.001))
	var q := c + d * t
	var dir := d.normalized()
	var side := Vector2(-dir.y, dir.x)
	var tri := PackedVector2Array([q + dir * 8.0, q - dir * 5.0 + side * 6.0, q - dir * 5.0 - side * 6.0])
	draw_colored_polygon(tri, col)
	tri.append(tri[0])
	draw_polyline(tri, DUSK, 1.5, true)

func _draw_player(player: Vector2) -> void:
	var yaw: float = float(state.get("player_yaw", 0.0))
	var dir := Vector2(-sin(yaw), -cos(yaw))
	var side := Vector2(-dir.y, dir.x)
	var c := _to_map(player)
	var tri := PackedVector2Array([c + dir * 10.0, c - dir * 6.0 + side * 7.0, c - dir * 3.0, c - dir * 6.0 - side * 7.0])
	draw_circle(c, 12.0, Color(CREAM, 0.12))
	draw_colored_polygon(tri, CREAM)
	tri.append(tri[0])
	draw_polyline(tri, DUSK, 1.5, true)

extends RefCounted
## One central, idempotent input map for keyboard, mouse and standard controllers.

static func configure() -> void:
	var keys: Dictionary = {
		"move_left": [KEY_A, KEY_LEFT], "move_right": [KEY_D, KEY_RIGHT],
		"move_forward": [KEY_W, KEY_UP], "move_back": [KEY_S, KEY_DOWN],
		"sprint": [KEY_SHIFT], "jump": [KEY_SPACE], "interact": [KEY_F],
		"brake": [KEY_SPACE], "boost": [KEY_SHIFT], "map": [KEY_M],
		"pause": [KEY_ESCAPE], "camera": [KEY_V], "radio": [KEY_R],
		"horn": [KEY_H], "headlights": [KEY_L], "photo": [KEY_F6],
		"recover": [KEY_T], "reload": [KEY_R],
		"throttle": [KEY_W, KEY_UP], "reverse": [KEY_S, KEY_DOWN],
		"ui_left": [KEY_LEFT], "ui_right": [KEY_RIGHT],
		"ui_up": [KEY_UP], "ui_down": [KEY_DOWN],
		"ui_accept": [KEY_ENTER, KEY_KP_ENTER], "ui_cancel": [KEY_ESCAPE],
	}
	for action: String in keys:
		_ensure_action(action)
		for key: int in keys[action]:
			var event: InputEventKey = InputEventKey.new()
			event.physical_keycode = key
			_add_event(action, event)
	var buttons: Dictionary = {
		"sprint": JOY_BUTTON_LEFT_STICK, "jump": JOY_BUTTON_A,
		"interact": JOY_BUTTON_Y, "brake": JOY_BUTTON_B,
		"boost": JOY_BUTTON_A, "map": JOY_BUTTON_BACK,
		"pause": JOY_BUTTON_START, "camera": JOY_BUTTON_RIGHT_STICK,
		"radio": JOY_BUTTON_DPAD_RIGHT, "horn": JOY_BUTTON_LEFT_SHOULDER,
		"headlights": JOY_BUTTON_DPAD_UP, "recover": JOY_BUTTON_DPAD_DOWN,
		"reload": JOY_BUTTON_X, "ui_accept": JOY_BUTTON_A,
		"ui_cancel": JOY_BUTTON_B, "ui_left": JOY_BUTTON_DPAD_LEFT,
		"ui_right": JOY_BUTTON_DPAD_RIGHT, "ui_up": JOY_BUTTON_DPAD_UP,
		"ui_down": JOY_BUTTON_DPAD_DOWN,
		"ui_focus_next": JOY_BUTTON_RIGHT_SHOULDER,
		"ui_focus_prev": JOY_BUTTON_LEFT_SHOULDER,
	}
	for action: String in buttons:
		var event: InputEventJoypadButton = InputEventJoypadButton.new()
		event.device = -1
		event.button_index = int(buttons[action])
		_add_event(action, event)
	_bind_axis("move_left", JOY_AXIS_LEFT_X, -1.0)
	_bind_axis("move_right", JOY_AXIS_LEFT_X, 1.0)
	_bind_axis("move_forward", JOY_AXIS_LEFT_Y, -1.0)
	_bind_axis("move_back", JOY_AXIS_LEFT_Y, 1.0)
	_bind_axis("ui_left", JOY_AXIS_LEFT_X, -1.0)
	_bind_axis("ui_right", JOY_AXIS_LEFT_X, 1.0)
	_bind_axis("ui_up", JOY_AXIS_LEFT_Y, -1.0)
	_bind_axis("ui_down", JOY_AXIS_LEFT_Y, 1.0)
	_bind_axis("aim", JOY_AXIS_TRIGGER_LEFT, 1.0)
	_bind_axis("fire", JOY_AXIS_TRIGGER_RIGHT, 1.0)
	_bind_axis("throttle", JOY_AXIS_TRIGGER_RIGHT, 1.0)
	_bind_axis("reverse", JOY_AXIS_TRIGGER_LEFT, 1.0)
	_bind_axis("look_left", JOY_AXIS_RIGHT_X, -1.0)
	_bind_axis("look_right", JOY_AXIS_RIGHT_X, 1.0)
	_bind_axis("look_up", JOY_AXIS_RIGHT_Y, -1.0)
	_bind_axis("look_down", JOY_AXIS_RIGHT_Y, 1.0)
	for action: String in ["aim", "fire"]:
		var event: InputEventMouseButton = InputEventMouseButton.new()
		event.button_index = MOUSE_BUTTON_RIGHT if action == "aim" else MOUSE_BUTTON_LEFT
		_add_event(action, event)


static func _ensure_action(action: String) -> void:
	if not InputMap.has_action(action):
		InputMap.add_action(action, 0.18)


static func _add_event(action: String, event: InputEvent) -> void:
	_ensure_action(action)
	if not InputMap.action_has_event(action, event):
		InputMap.action_add_event(action, event)


static func _bind_axis(action: String, axis: int, direction: float) -> void:
	var event: InputEventJoypadMotion = InputEventJoypadMotion.new()
	event.device = -1
	event.axis = axis
	event.axis_value = direction
	_add_event(action, event)


static func control_hint(action: String, controller: bool) -> String:
	var keyboard: Dictionary = {
		"move": "WASD / 方向键", "look": "鼠标", "move_left": "A / ←",
		"move_right": "D / →", "move_forward": "W / ↑", "move_back": "S / ↓",
		"sprint": "Shift", "jump": "空格", "interact": "F", "brake": "空格",
		"boost": "Shift", "aim": "鼠标右键", "fire": "鼠标左键", "map": "M",
		"pause": "Esc", "camera": "V", "radio": "R", "horn": "H",
		"headlights": "L", "photo": "F6", "recover": "T", "reload": "R",
		"throttle": "W", "reverse": "S", "ui_accept": "Enter", "ui_cancel": "Esc",
	}
	var gamepad: Dictionary = {
		"move": "左摇杆", "look": "右摇杆", "move_left": "左摇杆 ←",
		"move_right": "左摇杆 →", "move_forward": "左摇杆 ↑", "move_back": "左摇杆 ↓",
		"sprint": "按左摇杆", "jump": "A", "interact": "Y", "brake": "B",
		"boost": "A", "aim": "LT", "fire": "RT", "map": "View / Back",
		"pause": "Menu / Start", "camera": "按右摇杆", "radio": "十字键 →",
		"horn": "LB", "headlights": "十字键 ↑", "photo": "F6",
		"recover": "十字键 ↓", "reload": "X", "throttle": "RT", "reverse": "LT",
		"ui_accept": "A", "ui_cancel": "B",
	}
	var hints: Dictionary = gamepad if controller else keyboard
	return str(hints.get(action, action))

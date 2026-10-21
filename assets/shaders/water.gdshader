shader_type spatial;
render_mode cull_disabled, depth_draw_opaque;

uniform vec4 shallow_color : source_color = vec4(0.08, 0.39, 0.39, 1.0);
uniform vec4 deep_color : source_color = vec4(0.025, 0.16, 0.23, 1.0);
uniform vec4 sunset_color : source_color = vec4(1.0, 0.54, 0.27, 1.0);
uniform float night_mix : hint_range(0.0, 1.0) = 0.0;
varying vec3 world_position;

void vertex() {
    world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
    float wave = sin(world_position.x * 0.075 + TIME * 0.72) * 0.16;
    wave += sin(world_position.z * 0.11 + world_position.x * 0.04 + TIME * 0.48) * 0.11;
    VERTEX.y += wave;
}

void fragment() {
    float ripple = sin(world_position.x * 0.4 + world_position.z * 0.2 + TIME * 1.1);
    ripple += sin(world_position.x * 0.16 - world_position.z * 0.31 + TIME * 0.82);
    vec3 water = mix(shallow_color.rgb, deep_color.rgb, clamp((world_position.x - 310.0) / 280.0, 0.0, 1.0));
    float glitter = pow(max(0.0, sin(world_position.z * 0.42 + ripple * 1.3)), 28.0);
    float sun_path = exp(-pow((world_position.z + 150.0) / (100.0 + max(0.0, world_position.x - 310.0) * 0.22), 2.0));
    water += sunset_color.rgb * glitter * sun_path * 0.2 * (1.0 - night_mix);
    ALBEDO = water * mix(1.0, 0.32, night_mix);
    METALLIC = 0.25;
    ROUGHNESS = 0.25;
    NORMAL_MAP = normalize(vec3(ripple * 0.12, sin(world_position.z * 0.21 + TIME) * 0.16, 1.0)) * 0.5 + 0.5;
    NORMAL_MAP_DEPTH = 0.45;
    EMISSION = sunset_color.rgb * glitter * sun_path * 0.065 * (1.0 - night_mix);
}

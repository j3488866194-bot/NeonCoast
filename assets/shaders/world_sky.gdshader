shader_type sky;
render_mode use_debanding;

uniform vec3 sun_direction = vec3(0.86, 0.25, -0.44);
uniform float night_mix : hint_range(0.0, 1.0) = 0.0;
uniform float day_mix : hint_range(0.0, 1.0) = 0.0;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}
void sky() {
    float y = max(EYEDIR.y, 0.0);
    float toward_sun = pow(max(dot(normalize(vec3(EYEDIR.x, 0.02, EYEDIR.z)), normalize(vec3(sun_direction.x, 0.02, sun_direction.z))), 0.0), 3.0);
    vec3 horizon = mix(vec3(0.53, 0.47, 0.64), vec3(1.0, 0.56, 0.32), toward_sun);
    vec3 zenith = vec3(0.12, 0.29, 0.49);
    vec3 color = mix(horizon, zenith, pow(y, 0.42));
    color = mix(color, mix(vec3(0.62, 0.81, 0.88), vec3(0.1, 0.41, 0.7), pow(y, 0.5)), day_mix);
    color = mix(color, mix(vec3(0.07, 0.09, 0.2), vec3(0.012, 0.025, 0.075), pow(y, 0.4)), night_mix);
    float sun_dot = max(dot(EYEDIR, normalize(sun_direction)), 0.0);
    color += vec3(1.0, 0.65, 0.32) * pow(sun_dot, 32.0) * 0.32 * (1.0 - night_mix);
    color += vec3(1.0, 0.78, 0.45) * smoothstep(0.99955, 0.99988, sun_dot) * 2.8 * (1.0 - night_mix);
    if (EYEDIR.y > 0.025) {
        vec2 cloud_uv = EYEDIR.xz / (EYEDIR.y + 0.14) * vec2(1.6, 6.0);
        float clouds = noise(cloud_uv) * 0.6 + noise(cloud_uv * 2.13) * 0.27 + noise(cloud_uv * 4.1) * 0.13;
        float cloud_mask = smoothstep(0.56, 0.75, clouds) * smoothstep(0.7, 0.1, y);
        vec3 cloud_color = mix(vec3(0.93, 0.63, 0.54), vec3(0.1, 0.12, 0.2), night_mix);
        cloud_color = mix(cloud_color, vec3(0.96, 0.95, 0.92), day_mix);
        color = mix(color, cloud_color, cloud_mask * 0.6);
        float stars = pow(hash(floor(EYEDIR.xz / max(EYEDIR.y, 0.1) * 850.0)), 1100.0);
        color += vec3(stars) * night_mix * smoothstep(0.02, 0.35, y) * 0.7;
    }
    COLOR = color;
}

/**
 * @author 60Trees_ (github.com/60Trees)
 * @version 1.0.0
 */

uniform uint u_time;

out vec4 FragColor;
uniform ivec2 u_windowsize;
uniform sampler2D u_texture;
in vec2 uv;
uniform bool u_atlas;
uniform vec4 u_selected_rect;

void main() {
    vec4 color = vec4(texture(u_texture, uv).rgb, 1.0);
    if (u_atlas) {
        const float border = 0.004;
        const bool inside = uv.x >= u_selected_rect.x - border &&
            uv.x <= u_selected_rect.y + border &&
            uv.y >= u_selected_rect.z - border &&
            uv.y <= u_selected_rect.w + border;
        const bool edge = uv.x <= u_selected_rect.x + border ||
            uv.x >= u_selected_rect.y - border ||
            uv.y <= u_selected_rect.z + border ||
            uv.y >= u_selected_rect.w - border;
        if (inside && edge) color = vec4(1.0, 0.85, 0.1, 1.0);
    }
    FragColor = color;
}

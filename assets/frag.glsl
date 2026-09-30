/**
 * @author 60Trees_ (github.com/60Trees)
 * @version 1.0.0
 */

uniform uint u_time;

out vec4 FragColor;
uniform ivec2 u_windowsize;
uniform sampler2D u_texture;
in vec2 uv;

void main() {
    FragColor = vec4(texture(u_texture, uv).rgb, 1.0);
}

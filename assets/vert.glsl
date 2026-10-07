/**
 * @author 60Trees_ (github.com/60Trees)
 * @version 1.0.0
 */

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
out vec2 uv;
uniform mat4 u_transform;

void main() {
    gl_Position = u_transform * vec4(aPos, 1.0);
    uv = aUv;
}

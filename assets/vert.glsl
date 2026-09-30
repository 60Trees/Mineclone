/**
 * @author 60Trees_ (github.com/60Trees)
 * @version 1.0.0
 */

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
out vec2 uv;

void main() {
    gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);
    uv = aUv;
}

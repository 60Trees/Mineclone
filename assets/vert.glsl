#version auto

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
layout (location = 2) in vec2 aTexCoord;

out vec3 ourColor;
out vec2 TexCoord;
out vec3 WorldPosition;
uniform mat4 viewProjection;
uniform vec2 atlasSize;

void main() {
    gl_Position = viewProjection * vec4(aPos, 1.0);
    ourColor = aColor;
    TexCoord = aTexCoord / atlasSize;
    WorldPosition = aPos;
}

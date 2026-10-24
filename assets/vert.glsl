#version auto
layout (location = 0) in vec3 aPos;
layout (location = 1) in float aLight;
layout (location = 2) in vec2 aTileUV;    // 0..repeat across the face
layout (location = 3) in vec4 aTileRect;  // tile origin + size in atlas pixels

out float Light;
out vec2 TileUV;
flat out vec4 TileRect;  // identical for all verts of a face, so no interpolation
out vec3 WorldPosition;

uniform mat4 viewProjection;

void main() {
    gl_Position = viewProjection * vec4(aPos, 1.0);
    Light = aLight;
    TileUV = aTileUV;
    TileRect = aTileRect;
    WorldPosition = aPos;
}

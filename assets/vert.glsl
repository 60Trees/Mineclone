#version auto
layout (location = 0) in vec3 aPos;       // RAW chunk-local integer position (uint16 on the CPU), 16 units per block
layout (location = 1) in float aLight;    // normalized 0..1 (uint16 normalized by the GPU)
layout (location = 2) in vec2 aTileUV;    // 0..repeat across the face
layout (location = 3) in vec4 aTileRect;  // tile origin + size in atlas pixels

out float Light;
out vec2 TileUV;
flat out vec4 TileRect;  // identical for all verts of a face, so no interpolation
out vec3 RelativePosition;  // camera-relative position in world units (for fog)

uniform mat4 viewProjection;

// Per draw call (one draw call per chunk):
uniform vec3 chunkOffset;  // chunk origin minus camera, in world units (computed in double on the CPU)
uniform float chunkScale;  // world units per raw vertex unit = 2^lod / CHUNK_SIZE

void main() {
    // The vertex data is small exact integers; the only float math is one multiply-add against a
    // camera-relative offset, so there is no precision loss no matter how far from the origin we are.
    vec3 relative = aPos * chunkScale + chunkOffset;
    gl_Position = viewProjection * vec4(relative, 1.0);
    Light = aLight;
    TileUV = aTileUV;
    TileRect = aTileRect;
    RelativePosition = relative;
}

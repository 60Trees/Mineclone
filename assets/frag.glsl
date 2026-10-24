#version auto

out vec4 FragColor;
in float Light;
in vec2 TileUV;
flat in vec4 TileRect;
in vec3 WorldPosition;

uniform sampler2D ourTexture;
uniform vec2 atlasSize;
uniform vec3 cameraPosition;

// ===================== FOG TUNING =====================
// All distances are in blocks (world units).

// Color the terrain fades into. Match this to glClearColor
// (or your sky color) or distant terrain will look cut out.
const vec3 FOG_COLOR = vec3(0.0, 0.0, 0.0);

// Below this distance there is NO fog at all.
// Raise it to keep more of the nearby area crisp.
const float FOG_START = 30.0;

// At/after this distance the fog is 100% opaque.
// Set this to roughly the farthest terrain you want to see.
// Fog is spread over log(FOG_END / FOG_START), so each
// multiplicative step (x10, x100...) gets an equal share.
const float FOG_END = 30000000.0;

// Shape of the ramp between START and END (0..1 -> 0..1).
//   1.0 = linear in log-distance
//   >1  = fog stays thin longer, then ramps up late
//   <1  = fog ramps up early, then flattens
const float FOG_CURVE = 1.5;

// For exponentialFog() below:
// distance at which 63% of the light is lost (1 - 1/e).
const float FOG_SCALE = 400000.0;
// 1.0 = classic exponential. 2.0 = clearer up close and
// a sharper wall of fog near FOG_SCALE (like GL_EXP2).
const float FOG_POWER = 1.5;
// ======================================================

// Returns fog amount in [0, 1]. 0 = no fog, 1 = fully fog color.
// Fog is spread evenly across orders of magnitude of distance.
float logFog(float dist) {
    // max() avoids log(0) = -inf at dist == 0.
    float t = log2(max(dist, 0.001) / FOG_START) / log2(FOG_END / FOG_START);
    t = clamp(t, 0.0, 1.0);        // 0 before START, 1 after END
    t = pow(t, FOG_CURVE);         // shape the ramp
    return t * t * (3.0 - 2.0 * t); // smoothstep: no visible fog edges
}

// Alternative: physically-motivated exponential fog.
// transmittance = exp(-(dist / FOG_SCALE)^FOG_POWER)
float exponentialFog(float dist) {
    return 1.0 - exp(-pow(dist / FOG_SCALE, FOG_POWER));
}

// Single entry point. Swap which function is returned to change modes.
float computeFog(float dist) {
    return logFog(dist);
    // return exponentialFog(dist);
}

void main() {
    // The atlas can't use GL_REPEAT, so wrap inside the tile's rectangle by hand.
    vec2 local = fract(TileUV);                         // 0..1 within one repetition
    vec2 px = TileRect.xy + local * TileRect.zw;        // atlas pixel coordinate
    // Keep the sample strictly inside the tile so float rounding can't bleed into a neighbour texture.
    px = clamp(px, TileRect.xy + 0.01, TileRect.xy + TileRect.zw - 0.01);

    vec4 blockColor = texture(ourTexture, px / atlasSize) * vec4(vec3(Light), 1.0);
    float dist = distance(WorldPosition, cameraPosition);
    float fogFactor = computeFog(dist);
    FragColor = vec4(mix(blockColor.rgb, FOG_COLOR, fogFactor), blockColor.a);
}

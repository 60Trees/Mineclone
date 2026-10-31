#version auto

out vec4 FragColor;
in float Light;
in vec2 TileUV;
flat in vec4 TileRect;
in vec3 RelativePosition;  // camera-relative, so its length is the distance to the camera

uniform sampler2D ourTexture;
uniform vec2 atlasSize;

// ===================== FOG =====================
// Set by the renderer every frame. Distances are in blocks (world units).
uniform vec3 fogColor;   // same as the clear color, so distant terrain fades into the sky
uniform float fogStart;  // no fog at/below this distance
uniform float fogEnd;    // 100% fog at/after this distance (= farthest loaded mesh)

// Shape of the ramp between start and end (0..1 -> 0..1).
//   1.0 = linear in log-distance
//   >1  = fog stays thin longer, then ramps up late
//   <1  = fog ramps up early, then flattens
const float FOG_CURVE = 1.5;

// For exponentialFog(): 1.0 = classic exponential, 2.0 = GL_EXP2-like wall of fog.
const float FOG_POWER = 1.5;

// How quickly the directional shading is washed out by fog.
//   fogFactor at which the tint is completely gone (0..1). Lower = the tint disappears closer to the camera.
const float TINT_FADE_END = 0.3;
// What the light fades toward. 1.0 = fully lit; use something like 0.8 if distant terrain
// looks too bright compared to nearby terrain (roughly the average of your face shades).
const float TINT_NEUTRAL = 0.85;

// ===============================================

// Returns fog amount in [0, 1]. 0 = no fog, 1 = fully fog color.
// Spread evenly across orders of magnitude of distance, which matches how the LODs double in size.
float logFog(float dist) {
    float t = log2(max(dist, fogStart) / fogStart) / log2(fogEnd / fogStart);
    t = clamp(t, 0.0, 1.0);          // 0 before start, 1 after end
    t = pow(t, FOG_CURVE);           // shape the ramp
    return t * t * (3.0 - 2.0 * t);  // smoothstep: no visible fog edges
}

// Alternative: exponential fog, reaching ~95% at fogEnd.
float exponentialFog(float dist) {
    return 1.0 - exp(-pow(3.0 * dist / fogEnd, FOG_POWER));
}

// Single entry point. Swap which function is returned to change modes.
float computeFog(float dist) {
    return clamp(logFog(dist), 0.0, 1.0) * 0.9;
    // return exponentialFog(dist);
}

void main() {
    vec2 local = fract(TileUV);
    vec2 px = TileRect.xy + local * TileRect.zw;
    px = clamp(px, TileRect.xy + 0.01, TileRect.xy + TileRect.zw - 0.01);

    float fogFactor = computeFog(length(RelativePosition));

    // The more fog, the less the face-direction tint matters.
    float tintStrength = 1.0 - smoothstep(0.0, TINT_FADE_END, fogFactor);
    float light = mix(TINT_NEUTRAL, Light, tintStrength);

    vec4 blockColor = texture(ourTexture, px / atlasSize) * vec4(vec3(light), 1.0);
    FragColor = vec4(mix(blockColor.rgb, fogColor, fogFactor), blockColor.a);
}

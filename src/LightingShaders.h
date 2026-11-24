#pragma once
// The 3D objects' lighting, one shader program per light model (Unlit, Phong, Blinn-Phong, Toon). Each program is
// the same three parts: the common inputs and helpers, that model's shade() function, and the same main().
// The renderer compiles all four once and picks one with glUseProgram for each object.
//
// Uniforms every program expects (all in the model's world: model at the origin, radius 1, camera on +Z):
//   uTex, uIn                       the media texture, and the input quad (model texture position -> media)
//   uLighting                       0 = the picture as it is, 1 = fully lit (mixes the two)
//   uCamPos                         camera position
//   uAmbient, uKd, uKs, uShininess  ambient term and the material
//   uDirDir, uDirColor              directional light: direction TOWARDS the light, colour x strength
//   uPointPos, uPointColor, uPointRange
//   uSpotPos, uSpotDir, uSpotColor, uSpotCosOuter, uSpotCosInner   (uSpotDir points from the spot into the scene)
#include <string>

inline constexpr const char* kMeshVS = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
uniform mat4 uMVP;
uniform mat4 uModel;
out vec2 vUV;
out vec3 vN;
out vec3 vPos;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vUV = aUV;
    vN = mat3(uModel) * aNormal;
    vPos = (uModel * vec4(aPos, 1.0)).xyz;
}
)GLSL";

inline constexpr const char* kLightCommonFS = R"GLSL(#version 330 core
uniform sampler2D uTex;
uniform mat3  uIn;
uniform float uLighting;
uniform vec3  uCamPos;
uniform float uAmbient, uKd, uKs, uShininess;
uniform vec3  uDirDir, uDirColor;
uniform vec3  uPointPos, uPointColor;
uniform float uPointRange;
uniform vec3  uSpotPos, uSpotDir, uSpotColor;
uniform float uSpotCosOuter, uSpotCosInner;
in vec2 vUV;
in vec3 vN;
in vec3 vPos;
out vec4 frag;

vec3 baseColor() {
    vec3 m = uIn * vec3(vUV, 1.0);
    vec2 uv = m.xy / m.z;
    return texture(uTex, vec2(uv.x, 1.0 - uv.y)).rgb;
}
)GLSL";

// shade(N, V, L, C, base): the light C arriving from direction L (towards the light) at a surface with normal N,
// seen from direction V (towards the camera). Returns diffuse + specular.
inline constexpr const char* kShadeUnlitFS = R"GLSL(
vec3 shade(vec3 N, vec3 V, vec3 L, vec3 C, vec3 base) { return vec3(0.0); }
)GLSL";

inline constexpr const char* kShadePhongFS = R"GLSL(
vec3 shade(vec3 N, vec3 V, vec3 L, vec3 C, vec3 base) {
    float nl = max(dot(N, L), 0.0);
    vec3  R  = reflect(-L, N);                                   // Phong: the mirror direction against the view
    float s  = nl > 0.0 ? pow(max(dot(R, V), 0.0), uShininess) : 0.0;
    return C * (uKd * nl * base + uKs * s);
}
)GLSL";

inline constexpr const char* kShadeBlinnPhongFS = R"GLSL(
vec3 shade(vec3 N, vec3 V, vec3 L, vec3 C, vec3 base) {
    float nl = max(dot(N, L), 0.0);
    vec3  H  = normalize(L + V);                                 // Blinn: the half vector against the normal
    float s  = nl > 0.0 ? pow(max(dot(N, H), 0.0), uShininess) : 0.0;
    return C * (uKd * nl * base + uKs * s);
}
)GLSL";

inline constexpr const char* kShadeToonFS = R"GLSL(
vec3 shade(vec3 N, vec3 V, vec3 L, vec3 C, vec3 base) {
    float nl = max(dot(N, L), 0.0);
    float band = nl > 0.75 ? 1.0 : nl > 0.4 ? 0.6 : nl > 0.1 ? 0.3 : 0.0;     // a few flat steps, like a cartoon
    vec3  H  = normalize(L + V);
    float s  = (nl > 0.0 && pow(max(dot(N, H), 0.0), uShininess) > 0.5) ? 1.0 : 0.0;
    return C * (uKd * band * base + uKs * s);
}
)GLSL";

inline constexpr const char* kLightMainFS = R"GLSL(
void main() {
    vec3 base = baseColor();
    vec3 N = normalize(vN);
    vec3 V = normalize(uCamPos - vPos);
    if (dot(N, V) < 0.0) N = -N;                                 // the inside of an open model faces the camera too
    vec3 lit = uAmbient * base;
    lit += shade(N, V, normalize(uDirDir), uDirColor, base);
    vec3  toP = uPointPos - vPos;
    float dP  = length(toP);
    lit += shade(N, V, toP / max(dP, 1e-5), uPointColor / (1.0 + (dP * dP) / (uPointRange * uPointRange)), base);
    vec3  toS = uSpotPos - vPos;
    float dS  = length(toS);
    vec3  Ls  = toS / max(dS, 1e-5);
    float cone = smoothstep(uSpotCosOuter, uSpotCosInner, dot(-Ls, uSpotDir));
    lit += shade(N, V, Ls, uSpotColor * cone, base);
    frag = vec4(mix(base, lit, uLighting), 1.0);
}
)GLSL";

inline std::string lightingFragmentShader(int model) {
    const char* body = model == 0 ? kShadeUnlitFS : model == 1 ? kShadePhongFS : model == 3 ? kShadeToonFS : kShadeBlinnPhongFS;
    return std::string(kLightCommonFS) + body + kLightMainFS;
}

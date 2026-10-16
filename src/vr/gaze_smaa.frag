#version 450
#extension GL_GOOGLE_include_directive : require
layout(push_constant) uniform Focus {
    vec2 texel;
    vec2 center;
    float innerRadius;
    float outerRadius;
    int decodeOutput;
    int encodeOutput;
} focus;
#define SMAA_RT_METRICS vec4(focus.texel,1.0/focus.texel)
#define SMAA_GLSL_4
#ifdef NIGHTCAP_SMAA_HIGH
#define SMAA_PRESET_HIGH
#else
#define SMAA_PRESET_LOW
#endif
#include "smaa/SMAA.hlsl"
layout(set=0,binding=0) uniform sampler2D input0;
layout(set=0,binding=1) uniform sampler2D input1;
layout(set=0,binding=2) uniform sampler2D input2;
layout(location=0) out vec4 outputColor;
vec3 decodeSrgb(vec3 c) {
    return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),greaterThan(c,vec3(0.04045)));
}
vec3 encodeSrgb(vec3 c) {
    return mix(c*12.92,1.055*pow(max(c,vec3(0)),vec3(1.0/2.4))-0.055,greaterThan(c,vec3(0.0031308)));
}
void main() {
    vec2 uv=gl_FragCoord.xy*focus.texel;
#if NIGHTCAP_SMAA_PASS==0
    vec4 offsets[3];SMAAEdgeDetectionVS(uv,offsets);
    outputColor=vec4(SMAALumaEdgeDetectionPS(uv,offsets,input0),0,0);
#elif NIGHTCAP_SMAA_PASS==1
    vec2 pixel;vec4 offsets[3];SMAABlendingWeightCalculationVS(uv,pixel,offsets);
    outputColor=SMAABlendingWeightCalculationPS(uv,pixel,offsets,input0,input1,input2,vec4(0));
#else
    float weight=1.0-smoothstep(focus.innerRadius,focus.outerRadius,distance(uv,focus.center));
    if(weight<=0.0)discard;
    vec4 offset;SMAANeighborhoodBlendingVS(uv,offset);
    vec4 original=texture(input0,uv);
    vec3 result=mix(original.rgb,SMAANeighborhoodBlendingPS(uv,offset,input0,input1).rgb,weight);
    if(focus.decodeOutput!=0)result=decodeSrgb(result);
    if(focus.encodeOutput!=0)result=encodeSrgb(result);
    outputColor=vec4(result,original.a);
#endif
}

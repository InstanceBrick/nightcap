#version 450
#extension GL_GOOGLE_include_directive : require
#define A_GPU 1
#define A_GLSL 1
#include "fsr/ffx_a.h"
layout(set=0,binding=0) uniform sampler2D inputImage;
layout(push_constant) uniform Constants {
    uvec4 con0,con1,con2,con3;
    vec2 texel,center;
    float radius;
    uint inputSrgb,outputSrgb,reserved;
} pc;
layout(location=0) out vec4 outputColor;
vec4 encodeGamma(vec4 c) {
    return mix(c*12.92,1.055*pow(max(c,vec4(0)),vec4(1.0/2.4))-0.055,
               greaterThan(c,vec4(0.0031308)));
}
vec3 decodeGamma(vec3 c) {
    return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),greaterThan(c,vec3(0.04045)));
}
#if NIGHTCAP_FSR_PASS==0
#define FSR_EASU_F 1
vec4 FsrEasuRF(vec2 p) { vec4 v=textureGather(inputImage,p,0); return pc.inputSrgb!=0?encodeGamma(v):v; }
vec4 FsrEasuGF(vec2 p) { vec4 v=textureGather(inputImage,p,1); return pc.inputSrgb!=0?encodeGamma(v):v; }
vec4 FsrEasuBF(vec2 p) { vec4 v=textureGather(inputImage,p,2); return pc.inputSrgb!=0?encodeGamma(v):v; }
#else
#define FSR_RCAS_F 1
vec4 FsrRcasLoadF(ivec2 p) {
    vec4 v=texelFetch(inputImage,clamp(p,ivec2(0),textureSize(inputImage,0)-1),0);
    if(pc.inputSrgb!=0)v.rgb=encodeGamma(vec4(v.rgb,0)).rgb;
    return v;
}
void FsrRcasInputF(inout float r,inout float g,inout float b) {}
#endif
#include "fsr/ffx_fsr1.h"
void main() {
    uvec2 pixel=uvec2(gl_FragCoord.xy);
    vec3 color;
    float alpha;
#if NIGHTCAP_FSR_PASS==0
    vec2 sourcePixel=vec2(pixel)*uintBitsToFloat(pc.con0.xy)+uintBitsToFloat(pc.con0.zw);
    vec4 linearSample=texture(inputImage,(sourcePixel+0.5)*uintBitsToFloat(pc.con1.xy));
    float weight=1.0-smoothstep(pc.radius,pc.radius+0.04,
                              distance(gl_FragCoord.xy*pc.texel,pc.center));
    color=linearSample.rgb;
    if(weight>0.0) {
        vec3 reconstructed;
        FsrEasuF(reconstructed,pixel,pc.con0,pc.con1,pc.con2,pc.con3);
        color=mix(color,reconstructed,weight);
    }
    alpha=linearSample.a;
#else
    float weight=1.0-smoothstep(pc.radius*0.8,pc.radius,
                              distance(gl_FragCoord.xy*pc.texel,pc.center));
    if(weight<=0.0)discard;
    vec4 original=FsrRcasLoadF(ivec2(pixel));
    FsrRcasF(color.r,color.g,color.b,pixel,pc.con0);
    color=mix(original.rgb,clamp(color,vec3(0),vec3(1)),weight);
    alpha=original.a;
#endif
    color=clamp(color,vec3(0),vec3(1));
    if(pc.outputSrgb!=0)color=decodeGamma(color);
    outputColor=vec4(color,alpha);
}

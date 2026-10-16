#version 450
layout(set=0,binding=0) uniform sampler2D eyeTexture;
layout(push_constant) uniform Focus {
    vec2 texel;
    vec2 center;
    float innerRadius;
    float outerRadius;
    int decodeOutput;
    int encodeOutput;
} focus;
layout(location=0) out vec4 outputColor;
float luminance(vec3 c) { return dot(c,vec3(0.299,0.587,0.114)); }
vec3 sampleEye(vec2 uv) { return texture(eyeTexture,uv).rgb; }
vec3 decodeSrgb(vec3 c) {
    return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),greaterThan(c,vec3(0.04045)));
}
vec3 encodeSrgb(vec3 c) {
    return mix(c*12.92,1.055*pow(max(c,vec3(0)),vec3(1.0/2.4))-0.055,greaterThan(c,vec3(0.0031308)));
}
void main() {
    vec2 uv=gl_FragCoord.xy*focus.texel;
    float weight=1.0-smoothstep(focus.innerRadius,focus.outerRadius,distance(uv,focus.center));
    // The existing transfer copy supplies every pixel outside this disk.
    if(weight<=0.0) discard;
    vec4 original=texture(eyeTexture,uv);
    vec3 result=original.rgb;
    float nw=luminance(sampleEye(uv+vec2(-1,-1)*focus.texel));
    float ne=luminance(sampleEye(uv+vec2(1,-1)*focus.texel));
    float sw=luminance(sampleEye(uv+vec2(-1,1)*focus.texel));
    float se=luminance(sampleEye(uv+vec2(1,1)*focus.texel));
    float mid=luminance(result);
    float lo=min(mid,min(min(nw,ne),min(sw,se)));
    float hi=max(mid,max(max(nw,ne),max(sw,se)));
    // Leave low-contrast detail untouched; filter only detected edges.
    if(hi-lo>=max(0.045,hi*0.125)) {
        vec2 direction=vec2(-(nw+ne-sw-se),nw+sw-ne-se);
        float reduce=max((nw+ne+sw+se)*0.03125,0.0078125);
        direction=clamp(direction/(min(abs(direction.x),abs(direction.y))+reduce),vec2(-6),vec2(6))*focus.texel;
        vec3 a=0.5*(sampleEye(uv-direction/6.0)+sampleEye(uv+direction/6.0));
        vec3 b=a*0.5+0.25*(sampleEye(uv-direction*0.5)+sampleEye(uv+direction*0.5));
        float lb=luminance(b);
        result=mix(result,(lb<lo||lb>hi)?a:b,weight);
    }
    if(focus.decodeOutput!=0)result=decodeSrgb(result);
    if(focus.encodeOutput!=0)result=encodeSrgb(result);
    outputColor=vec4(result,original.a);
}

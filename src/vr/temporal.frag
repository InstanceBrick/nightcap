#version 450
layout(set=0,binding=0) uniform sampler2D currentImage;
layout(set=0,binding=1) uniform sampler2D historyImage;
layout(push_constant) uniform Constants {
    vec4 row0,row1,row2;
    vec2 texel,center,previousCenter;
    float radius,previousRadius,blend;
    uint outputSrgb;
    uvec2 reserved;
} pc;
layout(location=0) out vec4 outputColor;
vec3 decodeGamma(vec3 c) {
    return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),greaterThan(c,vec3(0.04045)));
}
void main() {
    vec2 uv=gl_FragCoord.xy*pc.texel;
    float gazeWeight=1.0-smoothstep(pc.radius*0.85,pc.radius,distance(uv,pc.center));
    if(gazeWeight<=0.0)discard;
    vec3 point=vec3(uv,1);
    float divisor=dot(pc.row2.xyz,point);
    if(divisor<=0.00001)discard;
    vec2 previousUv=vec2(dot(pc.row0.xyz,point),dot(pc.row1.xyz,point))/divisor;
    if(any(lessThan(previousUv,pc.texel))||any(greaterThan(previousUv,1.0-pc.texel))||
       distance(previousUv,pc.previousCenter)>pc.previousRadius)discard;
    ivec2 pixel=ivec2(gl_FragCoord.xy),limit=textureSize(currentImage,0)-1;
    vec4 current=texelFetch(currentImage,clamp(pixel,ivec2(0),limit),0);
    vec3 low=current.rgb,high=current.rgb;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        vec3 sampleColor=texelFetch(currentImage,clamp(pixel+ivec2(x,y),ivec2(0),limit),0).rgb;
        low=min(low,sampleColor);high=max(high,sampleColor);
    }
    vec4 history=texture(historyImage,previousUv);
    vec3 difference=abs(history.rgb-current.rgb);
    float colorChange=max(difference.r,max(difference.g,difference.b));
    float alphaChange=abs(history.a-current.a);
    float confidence=(1.0-smoothstep(0.02,0.10,colorChange))*(1.0-smoothstep(0.01,0.05,alphaChange));
    // A low, bounded two-frame contribution avoids accumulated sharpening,
    // long trails and stale peripheral history when the gaze moves.
    vec3 color=mix(current.rgb,clamp(history.rgb,low,high),pc.blend*confidence*gazeWeight);
    if(pc.outputSrgb!=0)color=decodeGamma(color);
    outputColor=vec4(color,current.a);
}

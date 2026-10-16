#version 450
layout(set=0,binding=0)uniform sampler2D fullColor;
layout(set=0,binding=1)uniform sampler2D focusColor;
layout(location=0)in vec2 uv;
layout(location=0)out vec4 color;
layout(push_constant)uniform Constants {
  vec2 fullInputSize;
  vec2 outputSize;
  vec2 cropOrigin;
  vec2 cropInputSize;
  vec2 gaze;
  float radius;
  float guard;
  uint alignedGrid;
} parameters;
void main(){
  color=textureLod(fullColor,uv,0);
  vec2 delta=(uv-parameters.gaze)*parameters.outputSize/min(parameters.outputSize.x,parameters.outputSize.y);
  float weight=1.0-smoothstep(parameters.radius,parameters.radius+parameters.guard,length(delta));
  if(weight<=0.0)return;
  // Preserve exact full-image pixel coordinates across integer crop movement.
  // Round only the input crop origin, never its projected output position.
  vec2 focusUV=(uv*parameters.fullInputSize-parameters.cropOrigin)/parameters.cropInputSize;
  if(any(lessThan(focusUV,vec2(0))) || any(greaterThan(focusUV,vec2(1))))return;
  vec4 focus;
  if(parameters.alignedGrid!=0u){
    // The ROI origin places FSR texel centres exactly on final-image centres.
    // Fetch directly: a linear sampler can still quantize an almost-integer
    // coordinate and soften high-contrast text on mobile texture hardware.
    ivec2 pixel=ivec2(floor(focusUV*vec2(textureSize(focusColor,0))));
    focus=texelFetch(focusColor,clamp(pixel,ivec2(0),textureSize(focusColor,0)-1),0);
  }else focus=textureLod(focusColor,focusUV,0);
  color=mix(color,focus,weight);
}

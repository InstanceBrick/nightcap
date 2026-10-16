#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mocktail::vr::internal {
struct QuantizedGazeCenter {unsigned x=0,y=0;};
inline QuantizedGazeCenter QuantizeGazeCenter(unsigned width,unsigned height,float x,float y) {
  if(!width || !height || !std::isfinite(x) || !std::isfinite(y))return {};
  return {unsigned(std::round((std::clamp(x,-1.f,1.f)+1.f)*.5f*width*4.f)),
          unsigned(std::round((1.f-std::clamp(y,-1.f,1.f))*.5f*height*4.f))};
}
inline float ClampGazeRadius(float radius) {
  return std::isfinite(radius)?std::clamp(radius,.08f,.35f):.22f;
}
inline bool ProjectGazeNdc(const float* gaze,const float* eye,const float* fov,float* x,float* y) {
  if(!gaze || !eye || !fov || !x || !y)return false;
  auto rotate=[](const float* q,const float* v,float* out) {
    float length=0;for(int i=0;i<4;++i){if(!std::isfinite(q[i]))return false;length+=q[i]*q[i];}
    if(length<1e-8f)return false;
    const float inverse=1.f/std::sqrt(length);
    const float a=q[0]*inverse,b=q[1]*inverse,c=q[2]*inverse,d=q[3]*inverse;
    const float tx=2.f*(b*v[2]-c*v[1]),ty=2.f*(c*v[0]-a*v[2]),tz=2.f*(a*v[1]-b*v[0]);
    out[0]=v[0]+d*tx+b*tz-c*ty;out[1]=v[1]+d*ty+c*tx-a*tz;out[2]=v[2]+d*tz+a*ty-b*tx;
    return true;
  };
  const float forward[3]={0,0,-1};float local[3],world[3];
  const float inverse_eye[4]={-eye[0],-eye[1],-eye[2],eye[3]};
  if(!rotate(gaze,forward,world) || !rotate(inverse_eye,world,local) || local[2]>=-1e-5f)return false;
  const float left=std::tan(fov[0]),right=std::tan(fov[1]),up=std::tan(fov[2]),down=std::tan(fov[3]);
  if(!std::isfinite(left+right+up+down) || right<=left || up<=down)return false;
  *x=2.f*(local[0]/-local[2]-left)/(right-left)-1.f;
  *y=2.f*(local[1]/-local[2]-down)/(up-down)-1.f;
  return std::isfinite(*x)&&std::isfinite(*y);
}
// OpenXR eye-foveation centers use NDC (+Y up). Vulkan framebuffer/image rows
// run downwards. Preserve a generous full-density disk around the actual gaze;
// reduce shading only outside it. Density maps store reciprocal fragment size.
inline void FillGazeDensityMap(std::vector<std::uint8_t>& pixels,unsigned width,unsigned height,
    float ndc_x,float ndc_y,int level=2,float radius=.22f,
    float region_half_width=0.f,float region_half_height=0.f) {
  pixels.assign(std::size_t(width)*height*2,255);
  if(!width || !height || !std::isfinite(ndc_x) || !std::isfinite(ndc_y) || level<=0)return;
  // Quarter-cell quantization avoids uploading new maps for tiny gaze jitter.
  // Its maximum position error is well inside the cell-diagonal guard below.
  const auto center=QuantizeGazeCenter(width,height,ndc_x,ndc_y);
  const float cx=center.x/(width*4.f),cy=center.y/(height*4.f);
  const float inner=ClampGazeRadius(radius);
  // Performance reduces detail sooner outside the same protected focus disk.
  const float outer=inner+(level==1?.36f:level==2?.26f:.10f);
  const float peripheral_work=level==1?.25f:.0625f;
  // A scalar ramp is quantized by the GPU into a few hard rings. Mix adjacent
  // supported fragment areas over a stable spatial pattern instead. This gives
  // intermediate average shading densities without temporal random noise or a
  // second full-resolution render. Alternate the elongated fragment direction
  // so intermediate stages do not consistently favour horizontal edges.
  constexpr float work[]={1.f,.5f,.25f,.125f,.0625f};
  constexpr unsigned sizes[][2]={{1,1},{1,2},{2,2},{2,4},{4,4}};
  constexpr unsigned threshold[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
  // Each map cell covers an area, not a point. Extend the full-detail region
  // by a cell diagonal so a gaze near a cell boundary cannot land in a coarse
  // fragment just outside the mathematical disk.
  const float margin=std::hypot(1.f/width,1.f/height);
  const float inner_squared=(inner+margin)*(inner+margin);
  const float outer_squared=(outer+margin)*(outer+margin);
  const auto peripheral_density=static_cast<std::uint8_t>(level==1?127:63);
  for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
    // A tiled GPU samples density at its region centre and applies it to the
    // entire region. Protect every region that could intersect the focus, not
    // just the tiny map cell containing the gaze. The bound comes from the
    // device's maxFragmentDensityTexelSize in framebuffer pixels.
    const float dx=std::max(0.f,std::abs((x+.5f)/width-cx)-region_half_width);
    const float dy=std::max(0.f,std::abs((y+.5f)/height-cy)-region_half_height);
    // Flat regions need no square root or density-stage search. The output
    // was initialized to full detail above; the peripheral reciprocal is exact.
    // Keep the existing hypot/ramp calculation throughout the transition.
    const float squared=dx*dx+dy*dy;
    if(squared<=inner_squared)continue;
    if(squared>=outer_squared) {
      const auto index=(std::size_t(y)*width+x)*2;
      pixels[index]=pixels[index+1]=peripheral_density;
      continue;
    }
    const float distance=std::hypot(dx,dy);
    float t=std::clamp((distance-inner-margin)/(outer-inner),0.f,1.f);
    t=t*t*(3.f-2.f*t);
    const float target=1.f-t*(1.f-peripheral_work);
    unsigned stage=0;
    while(stage<4 && target<work[stage+1])++stage;
    if(stage<4) {
      const float blend=(work[stage]-target)/(work[stage]-work[stage+1]);
      if(blend>(threshold[y%4][x%4]+.5f)/16.f)++stage;
    }
    const unsigned flip=(x+y)&1u;
    const auto index=(std::size_t(y)*width+x)*2;
    // Round density down: 128/255 requests an area smaller than 2 pixels,
    // which a conforming driver may clamp to 1; 127/255 actually permits 2.
    pixels[index]=static_cast<std::uint8_t>(255u/sizes[stage][flip]);
    pixels[index+1]=static_cast<std::uint8_t>(255u/sizes[stage][1u-flip]);
  }
}
inline std::vector<std::uint8_t> GazeDensityMap(unsigned width,unsigned height,
    float ndc_x,float ndc_y,int level=2,float radius=.22f,
    float region_half_width=0.f,float region_half_height=0.f) {
  std::vector<std::uint8_t> pixels;
  FillGazeDensityMap(pixels,width,height,ndc_x,ndc_y,level,radius,region_half_width,region_half_height);
  return pixels;
}

struct FramebufferDensityLayout {
  unsigned map_width=0,map_height=0,width=0,height=0;
  unsigned texel_width=0,texel_height=0,active_width=0,active_height=0;
  bool valid=false;
};
// Turnip samples at pixel>>ceil(log2(framebuffer/map)), clamped to the
// advertised power-of-two texel limits. Allocation padding is not scene UV.
inline FramebufferDensityLayout TurnipDensityLayout(unsigned map_width,unsigned map_height,
    unsigned width,unsigned height,unsigned min_width,unsigned min_height,
    unsigned max_width,unsigned max_height) {
  FramebufferDensityLayout result{map_width,map_height,width,height};
  const auto power=[](unsigned value){return value && !(value&(value-1));};
  if(!map_width || !map_height || !width || !height || !power(min_width) ||
      !power(min_height) || !power(max_width) || !power(max_height) ||
      min_width>max_width || min_height>max_height)return result;
  const auto step=[](unsigned pixels,unsigned cells,unsigned minimum,unsigned maximum) {
    unsigned value=minimum;
    while(value<maximum && value<pixels/cells)value*=2;
    return value;
  };
  result.texel_width=step(width,map_width,min_width,max_width);
  result.texel_height=step(height,map_height,min_height,max_height);
  result.active_width=1+(width-1)/result.texel_width;
  result.active_height=1+(height-1)/result.texel_height;
  result.valid=result.active_width<=map_width && result.active_height<=map_height;
  return result;
}
inline QuantizedGazeCenter QuantizeFramebufferGaze(const FramebufferDensityLayout& layout,
    float x,float y) {
  if(!layout.valid || !std::isfinite(x) || !std::isfinite(y))return {};
  return {unsigned(std::round((std::clamp(x,-1.f,1.f)+1.f)*.5f*layout.width*4.f/layout.texel_width)),
          unsigned(std::round((1.f-std::clamp(y,-1.f,1.f))*.5f*layout.height*4.f/layout.texel_height))};
}
struct GazeDensityMapKey {
  unsigned x=0,y=0,width=0,height=0,map_width=0,map_height=0,texel_width=0,texel_height=0;
  int level=0;float radius=0;
  unsigned guard_width=0,guard_height=0,color_format=0,depth_format=0,samples=0,attachments=0;
  bool operator==(const GazeDensityMapKey& other) const {
    return x==other.x && y==other.y && width==other.width && height==other.height &&
        map_width==other.map_width && map_height==other.map_height &&
        texel_width==other.texel_width && texel_height==other.texel_height &&
        level==other.level && radius==other.radius &&
        guard_width==other.guard_width && guard_height==other.guard_height &&
        color_format==other.color_format && depth_format==other.depth_format &&
        samples==other.samples && attachments==other.attachments;
  }
};
enum class GazeDensityCacheAction { kUpload,kReuse,kNeutral };
inline GazeDensityCacheAction DensityCacheAction(bool initialized,bool cached,
    std::uint64_t uploaded_frame,std::uint64_t frame,const GazeDensityMapKey& previous,
    const GazeDensityMapKey& requested) {
  if(initialized && cached && previous==requested)return GazeDensityCacheAction::kReuse;
  // A map already referenced in this frame cannot safely be overwritten for
  // another extent. Its pass uses the neutral map until the next frame.
  if(initialized && uploaded_frame==frame)return GazeDensityCacheAction::kNeutral;
  return GazeDensityCacheAction::kUpload;
}
inline bool FillFramebufferGazeDensityMap(std::vector<std::uint8_t>& pixels,
    const FramebufferDensityLayout& layout,float ndc_x,float ndc_y,int level,float radius,
    float region_half_width_pixels,float region_half_height_pixels) {
  pixels.assign(std::size_t(layout.map_width)*layout.map_height*2,255);
  if(!layout.valid || !std::isfinite(ndc_x) || !std::isfinite(ndc_y) ||
      !std::isfinite(region_half_width_pixels) || !std::isfinite(region_half_height_pixels) ||
      region_half_width_pixels<0 || region_half_height_pixels<0)return false;
  if(level<=0)return true;
  const auto center=QuantizeFramebufferGaze(layout,ndc_x,ndc_y);
  const float cx=float(center.x)*layout.texel_width/(4.f*layout.width);
  const float cy=float(center.y)*layout.texel_height/(4.f*layout.height);
  const float guard_x=region_half_width_pixels/layout.width;
  const float guard_y=region_half_height_pixels/layout.height;
  const float inner=ClampGazeRadius(radius);
  const float outer=inner+(level==1?.36f:level==2?.26f:.10f);
  const float margin=std::hypot(float(layout.texel_width)/layout.width,
                              float(layout.texel_height)/layout.height);
  const float inner_squared=(inner+margin)*(inner+margin);
  const float outer_squared=(outer+margin)*(outer+margin);
  const float peripheral_work=level==1?.25f:.0625f;
  const auto peripheral_density=static_cast<std::uint8_t>(level==1?127:63);
  constexpr float work[]={1.f,.5f,.25f,.125f,.0625f};
  constexpr unsigned sizes[][2]={{1,1},{1,2},{2,2},{2,4},{4,4}};
  constexpr unsigned threshold[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
  for(unsigned y=0;y<layout.active_height;++y)for(unsigned x=0;x<layout.active_width;++x) {
    const float px=(x+.5f)*layout.texel_width/layout.width;
    const float py=(y+.5f)*layout.texel_height/layout.height;
    const float dx=std::max(0.f,std::abs(px-cx)-guard_x);
    const float dy=std::max(0.f,std::abs(py-cy)-guard_y);
    const float squared=dx*dx+dy*dy;
    if(squared<=inner_squared)continue;
    const auto index=(std::size_t(y)*layout.map_width+x)*2;
    if(squared>=outer_squared) {pixels[index]=pixels[index+1]=peripheral_density;continue;}
    float t=std::clamp((std::hypot(dx,dy)-inner-margin)/(outer-inner),0.f,1.f);
    t=t*t*(3.f-2.f*t);
    const float target=1.f-t*(1.f-peripheral_work);
    unsigned stage=0;
    while(stage<4 && target<work[stage+1])++stage;
    if(stage<4 && (work[stage]-target)/(work[stage]-work[stage+1])>
        (threshold[y%4][x%4]+.5f)/16.f)++stage;
    const unsigned flip=(x+y)&1u;
    pixels[index]=static_cast<std::uint8_t>(255u/sizes[stage][flip]);
    pixels[index+1]=static_cast<std::uint8_t>(255u/sizes[stage][1u-flip]);
  }
  return true;
}
// The driver advertises R16G16_SFLOAT density attachments. These exact binary
// reciprocals avoid UNORM rounding and cover every area generated above.
inline void FillHalfDensityMap(std::vector<std::uint16_t>& result,const std::vector<std::uint8_t>& pixels) {
  result.resize(pixels.size());
  for(std::size_t i=0;i<pixels.size();++i)
    result[i]=pixels[i]==127?0x3800u:pixels[i]==63?0x3400u:0x3c00u;
}
inline std::vector<std::uint16_t> HalfDensityMap(const std::vector<std::uint8_t>& pixels) {
  std::vector<std::uint16_t> result;
  FillHalfDensityMap(result,pixels);
  return result;
}
}

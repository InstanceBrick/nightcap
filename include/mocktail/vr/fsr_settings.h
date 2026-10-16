#pragma once
#include <algorithm>
#include <cstdint>

namespace mocktail::vr {
struct FsrSettings {
  int quality=0;  // Custom scale, Ultra Quality, Quality, Balanced, Performance, Custom resolution.
  int scale_percent=80;
  std::uint32_t input_resolution=1728;
  bool operator==(const FsrSettings& other) const {
    return quality==other.quality && scale_percent==other.scale_percent &&
           input_resolution==other.input_resolution;
  }
  bool operator!=(const FsrSettings& other) const { return !(*this==other); }
};

inline std::uint32_t FsrRenderSize(std::uint32_t output, int mode,
                                  const FsrSettings& settings={}) {
  if((mode!=1 && mode!=2 && mode!=3) || output<2)return output;
  if(settings.quality==5)
    return std::clamp(settings.input_resolution,2u,output)&~1u;
  std::uint64_t numerator=std::clamp(settings.scale_percent,25,100),denominator=100;
  switch(settings.quality) {
    case 1:numerator=10;denominator=13;break;
    case 2:numerator=2;denominator=3;break;
    case 3:numerator=10;denominator=17;break;
    case 4:numerator=1;denominator=2;break;
    default:break;
  }
  const auto scaled=static_cast<std::uint32_t>(
      2*((std::uint64_t(output)*numerator+denominator)/(2*denominator)));
  return std::clamp(scaled,2u,output&~1u);
}
inline std::uint32_t FsrRenderHeight(std::uint32_t output_width,
                                    std::uint32_t output_height,int mode,
                                    const FsrSettings& settings={}) {
  if(settings.quality!=5 || mode==0 || !output_width || output_height<2)
    return FsrRenderSize(output_height,mode,settings);
  const auto width=FsrRenderSize(output_width,mode,settings);
  const auto height=static_cast<std::uint32_t>(
      2*((std::uint64_t(output_height)*width+output_width)/(2*std::uint64_t(output_width))));
  return std::clamp(height,2u,output_height&~1u);
}

}

#pragma once
#include "mocktail/vr/vr_pose_math.h"
#include <cmath>
#include <cstdint>

namespace mocktail::vr {
struct TemporalEyePose {
  float orientation[4]={0,0,0,1};
  float fov[4]={-1,1,1,-1};
  float position[3]={};
};

// Depthless rotational reprojection. Translation has no reliable depth-based
// correspondence, so reject history during appreciable eye translation.
inline bool TemporalReprojection(const TemporalEyePose& current,
                                 const TemporalEyePose& previous,
                                 float rows[3][4]) {
  pose::Quat c,p;
  if(!pose::QuatNormalize({current.orientation[0],current.orientation[1],current.orientation[2],current.orientation[3]},&c) ||
     !pose::QuatNormalize({previous.orientation[0],previous.orientation[1],previous.orientation[2],previous.orientation[3]},&p))return false;
  float distance=0;
  for(int i=0;i<3;++i){const float d=current.position[i]-previous.position[i];if(!std::isfinite(d))return false;distance+=d*d;}
  if(distance>.0025f*.0025f)return false;
  const auto rotation=pose::QuatMultiply(pose::QuatConjugate(p),c);
  if(std::abs(rotation.w)<.99905f)return false; // Reject large jumps (>5 degrees/frame).
  float f[4],old[4];
  for(int i=0;i<4;++i){f[i]=std::tan(current.fov[i]);old[i]=std::tan(previous.fov[i]);if(!std::isfinite(f[i]+old[i]))return false;}
  if(f[1]<=f[0] || f[2]<=f[3] || old[1]<=old[0] || old[2]<=old[3])return false;
  const pose::Vec3 basis[3]={{f[1]-f[0],0,0},{0,f[3]-f[2],0},{f[0],f[2],-1}};
  for(int column=0;column<3;++column){
    const auto ray=pose::QuatRotate(rotation,basis[column]);
    rows[0][column]=(ray.x+old[0]*ray.z)/(old[1]-old[0]);
    rows[1][column]=(-old[2]*ray.z-ray.y)/(old[2]-old[3]);
    rows[2][column]=-ray.z;
  }
  for(int i=0;i<3;++i)rows[i][3]=0;
  return true;
}
}

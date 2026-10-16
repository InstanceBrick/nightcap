#ifndef MOCKTAIL_VR_EYE_RESIZE_BOUNDARY_H_
#define MOCKTAIL_VR_EYE_RESIZE_BOUNDARY_H_

namespace mocktail::vr::internal {
// Eye getters return borrowed render targets. Replacing those targets after
// either eye has been requested can invalidate work recorded earlier in the
// same frame, even if all previously submitted GPU work has finished.
class EyeResizeBoundary {
 public:
  void OnPresent() { presented_ = true; eyes_used_ = false; }
  void OnEyeGetter() { eyes_used_ = true; }
  bool CanResize() const { return presented_ && !eyes_used_; }
 private:
  bool presented_ = false;
  bool eyes_used_ = false;
};
}  // namespace mocktail::vr::internal
#endif

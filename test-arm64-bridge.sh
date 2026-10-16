#!/bin/bash
set -euo pipefail
cmake -S /work -B /work/build-frame-arm64 -DBUILD_TESTING=ON
cmake --build /work/build-frame-arm64 --target roblox_vr_device_bridge_test arm64_guest_abi_test vr_pose_math_test xr_controller_test openxr_backend_test roblox_experience_composition_test vulkan_xr_recreation_probe roblox_output_device_bridge_test gaze_aa_gpu_test scene_foveation_gpu_test jnivm_test host_abi_profile_test -j3
/work/build-frame-arm64/tests/arm64_guest_abi_test
/work/build-frame-arm64/tests/roblox_vr_device_bridge_test
/work/build-frame-arm64/tests/vr_pose_math_test
/work/build-frame-arm64/tests/xr_controller_test
/work/build-frame-arm64/tests/openxr_backend_test
/work/build-frame-arm64/tests/roblox_experience_composition_test

/work/build-frame-arm64/tests/roblox_output_device_bridge_test

/work/build-frame-arm64/tests/jnivm_test
/work/build-frame-arm64/tests/host_abi_profile_test

# Upstream presentation, JNI dispatch, headset discovery and haptic protocol.
cmake --build /work/build-frame-arm64 --target vr_queue_submission_test jnivm_fmod_write_allocation_test jnivm_webrtc_audio_manager_test runtime_config_test runtime_config_file_test roblox_haptic_protocol_bridge_test vr_mirror_test window_pointer_capture_owner_test performance_policy_test -j3
for test in jnivm_fmod_write_allocation_test jnivm_webrtc_audio_manager_test runtime_config_test runtime_config_file_test roblox_haptic_protocol_bridge_test vr_mirror_test window_pointer_capture_owner_test performance_policy_test; do
  "/work/build-frame-arm64/tests/$test"
done
ctest --test-dir /work/build-frame-arm64 --output-on-failure -R '^VrQueueSubmission\.'
python3 /work/tests/derive_roblox_host_abi_profile_test.py

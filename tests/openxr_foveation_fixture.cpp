/* Actual Vulkan foveation dispatch qualification; no runtime or GPU is used. */
#define main ImportedFoveationVulkanBoundaryMain
#include "vr_openxr_vulkan_fixture.cpp"
#undef main

namespace {
static XrSwapchain updated_chains[128];
static XrFoveationProfileFB updated_profiles[128];
static int update_count, fail_update_a, fail_update_b;
static int eye_queries;
static XrFoveationEyeTrackedStateFlagsMETA eye_flags;
static XrVector2f eye_centers[XR_FOVEATION_CENTER_SIZE_META];

static XrResult XRAPI_PTR update_foveation(XrSwapchain chain, const XrSwapchainStateBaseHeaderFB *base) {
 assert(base && base->type==XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB);
 assert(update_count<128);
 const auto *state=reinterpret_cast<const XrSwapchainStateFoveationFB *>(base);
 updated_chains[update_count]=chain;updated_profiles[update_count]=state->profile;
 ++update_count;
 if(update_count==fail_update_a || update_count==fail_update_b) return XR_ERROR_RUNTIME_FAILURE;
 return XR_SUCCESS;
}
static XrResult XRAPI_PTR query_eye(XrSession session, XrFoveationEyeTrackedStateMETA *state) {
 assert(session==g.session && state->type==XR_TYPE_FOVEATION_EYE_TRACKED_STATE_META);
 ++eye_queries;state->flags=eye_flags;
 for(uint32_t i=0;i<XR_FOVEATION_CENTER_SIZE_META;++i) state->foveationCenter[i]=eye_centers[i];
 return XR_SUCCESS;
}
static XrFoveationProfileFB profile(uintptr_t value) { return reinterpret_cast<XrFoveationProfileFB>(value); }
static XrSwapchain swapchain(uintptr_t value) { return reinterpret_cast<XrSwapchain>(value); }
static XrSession session(uintptr_t value) { return reinterpret_cast<XrSession>(value); }

static void prepare(uint32_t layers, bool eye_available=true, bool fixed_available=true) {
 assert(layers==1 || layers==2);
 reset();update_count=fail_update_a=fail_update_b=eye_queries=0;
 eye_flags=XR_FOVEATION_EYE_TRACKED_STATE_VALID_BIT_META;
 eye_centers[0]={0.1f,0.2f};eye_centers[1]={-0.3f,0.4f};
 g.useVulkan=true;g.initialized=g.sessionRunning=g.frameBegun=g.shouldRender=true;
 g.session=session(99);g.sessionState=XR_SESSION_STATE_FOCUSED;
 g.vk.densityMaps=true;g.vk.arrayLayers=layers;
 g.foveationOff=profile(301);g.foveationFixed=profile(302);g.foveationEye=profile(303);
 g.foveationFixedAvailable=fixed_available;g.foveationEyeAvailable=eye_available;
 g.xr.UpdateSwapchain=update_foveation;g.xr.FoveationEyeTrackedState=query_eye;
 const int count=layers==2 ? 1 : 2;
 for(int i=0;i<count;++i) {
  g.chain[i].handle=swapchain(201+i);g.chain[i].acquired=g.chain[i].waited=true;
  g.chain[i].copiedMask=0;
 }
}
static void assert_update(int index, XrSwapchain chain, XrFoveationProfileFB selected) {
 assert(index>=0 && index<update_count);
 assert(updated_chains[index]==chain && updated_profiles[index]==selected);
}
static int invoke(int mode, int allow_eye) {
 float centers[2][2]={{9.f,9.f},{9.f,9.f}};
 int result=VRXR_UpdateVulkanFoveation(mode,allow_eye,centers);
 if(result!=2) for(int eye=0;eye<2;++eye) for(int axis=0;axis<2;++axis) assert(centers[eye][axis]==0.f);
 else for(int eye=0;eye<2;++eye) for(int axis=0;axis<2;++axis)
  assert(centers[eye][axis]==(axis==0 ? eye_centers[eye].x : eye_centers[eye].y));
 return result;
}
static void assert_no_fixed_profile() {
 for(int i=0;i<update_count;++i) assert(updated_profiles[i]!=g.foveationFixed);
}
static void three_eye_samples() {
 assert(invoke(2,1)==0 && g.foveationEyePolicy.stable_frames==1);
 eye_centers[0]={0.25f,0.5f};eye_centers[1]={-0.25f,-0.5f};
 assert(invoke(2,1)==0 && g.foveationEyePolicy.stable_frames==2);
 eye_centers[0]={0.75f,0.125f};eye_centers[1]={-0.75f,-0.125f};
 assert(invoke(2,1)==2 && g.foveationEyePolicy.stable_frames==3);
}
}

int main() {
 prepare(2,false,true);
 assert(invoke(0,1)==0);assert(invoke(2,1)==0);assert(invoke(87,1)==0);
 assert(eye_queries==0 && update_count==1);assert_update(0,swapchain(201),g.foveationOff);assert_no_fixed_profile();

 prepare(2,true,true);
 assert(invoke(1,0)==1 && eye_queries==0 && update_count==1);
 assert_update(0,swapchain(201),g.foveationFixed);

 prepare(2,true,true);three_eye_samples();
 assert(eye_queries==3 && update_count==5);
 assert_update(0,swapchain(201),g.foveationEye);assert_update(1,swapchain(201),g.foveationOff);
 assert_update(2,swapchain(201),g.foveationEye);assert_update(3,swapchain(201),g.foveationOff);
 assert_update(4,swapchain(201),g.foveationEye);
 eye_centers[0]={-0.1f,0.9f};eye_centers[1]={0.2f,-0.8f};
 assert(invoke(2,1)==2 && eye_queries==4); // each active eye call publishes a fresh META sample
 assert_no_fixed_profile();

 assert(invoke(2,0)==0 && g.foveationEyePolicy.stable_frames==0);
 three_eye_samples();
 g.sessionState=XR_SESSION_STATE_VISIBLE;
 assert(invoke(2,1)==0 && g.foveationEyePolicy.stable_frames==0);
 g.sessionState=XR_SESSION_STATE_FOCUSED;three_eye_samples();
 eye_flags=0;
 assert(invoke(2,1)==0 && g.foveationEyePolicy.stable_frames==0);
 eye_flags=XR_FOVEATION_EYE_TRACKED_STATE_VALID_BIT_META;three_eye_samples();
 eye_centers[1]={NAN,0.2f};
 assert(invoke(2,1)==0 && g.foveationEyePolicy.stable_frames==0);
 eye_centers[1]={0.2f,1.01f};
 assert(invoke(2,1)==0 && g.foveationEyePolicy.stable_frames==0);
 eye_centers[1]={0.2f,0.3f};three_eye_samples();assert_no_fixed_profile();

 prepare(1,true,true);
 assert(invoke(1,0)==1 && update_count==2);
 assert_update(0,swapchain(201),g.foveationFixed);assert_update(1,swapchain(202),g.foveationFixed);

 prepare(1,true,true);fail_update_a=2;
 assert(invoke(2,1)==0 && update_count==4 && eye_queries==0);
 assert_update(0,swapchain(201),g.foveationEye);assert_update(1,swapchain(202),g.foveationEye);
 assert_update(2,swapchain(201),g.foveationOff);assert_update(3,swapchain(202),g.foveationOff);
 assert_no_fixed_profile();

 prepare(1,true,true);fail_update_a=2;fail_update_b=3;
 assert(invoke(2,1)==-1 && update_count==3 && eye_queries==0);
 assert_update(0,swapchain(201),g.foveationEye);assert_update(1,swapchain(202),g.foveationEye);
 assert_update(2,swapchain(201),g.foveationOff);

 prepare(2,true,true);three_eye_samples();
 const int prior_updates=update_count, prior_queries=eye_queries;
 g.frameBegun=false;
 assert(invoke(2,1)==-1 && update_count==prior_updates && eye_queries==prior_queries);
 assert(g.foveationEyePolicy.stable_frames==0);
 g.frameBegun=true;three_eye_samples();
 const int acquired_updates=update_count, acquired_queries=eye_queries;
 g.chain[0].copiedMask=1;
 assert(invoke(2,1)==-1 && update_count==acquired_updates && eye_queries==acquired_queries);
 assert(g.foveationEyePolicy.stable_frames==0);
 g.chain[0].copiedMask=0;three_eye_samples();assert(eye_queries==9);

 puts("OpenXR foveation prepared-dispatch fixture passed; borrowed images, selection, KHR GPU protection, and FB/META runtime/gaze/device behavior remain unproven");
 return 0;
}

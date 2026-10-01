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
struct ProfileCreateObservation {
 XrStructureType root_type, first_type, second_type;
 XrFoveationLevelFB level;
 XrFoveationDynamicFB dynamic;
 float vertical_offset;
 bool root_has_next, first_has_next, second_has_next;
};
static ProfileCreateObservation profile_create_observations[16];
static XrResult profile_create_results[16];
static XrFoveationProfileFB profile_create_outputs[16];
static XrFoveationProfileFB destroyed_profiles[16];
static int profile_create_count, destroyed_profile_count;

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

static XrResult XRAPI_PTR create_profile(XrSession supplied_session,
 const XrFoveationProfileCreateInfoFB *info, XrFoveationProfileFB *out) {
 assert(supplied_session==g.session && info && out);
 assert(profile_create_count<16);
 const int index=profile_create_count++;
 ProfileCreateObservation &observed=profile_create_observations[index];
 observed.root_type=info->type;observed.root_has_next=info->next!=0;
 if(info->next) {
  const XrBaseInStructure *first=reinterpret_cast<const XrBaseInStructure *>(info->next);
  observed.first_type=first->type;
  if(first->type==XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB) {
   const XrFoveationLevelProfileCreateInfoFB *level=
    reinterpret_cast<const XrFoveationLevelProfileCreateInfoFB *>(first);
   observed.level=level->level;observed.vertical_offset=level->verticalOffset;
   observed.dynamic=level->dynamic;observed.first_has_next=level->next!=0;
   if(level->next) {
    const XrBaseInStructure *second=reinterpret_cast<const XrBaseInStructure *>(level->next);
    observed.second_type=second->type;
    observed.second_has_next=second->next!=0;
   }
  }
 }
 *out=profile(401+index);
 profile_create_outputs[index]=*out;
 return profile_create_results[index];
}
static XrResult XRAPI_PTR destroy_profile(XrFoveationProfileFB handle) {
 assert(handle && destroyed_profile_count<16);
 bool owned=false;
 for(int i=0;i<profile_create_count;++i)
  if(XR_SUCCEEDED(profile_create_results[i]) && profile_create_outputs[i]==handle) owned=true;
 assert(owned);
 for(int i=0;i<destroyed_profile_count;++i) assert(destroyed_profiles[i]!=handle);
 destroyed_profiles[destroyed_profile_count++]=handle;
 return XR_SUCCESS;
}

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
static void prepare_profile_creation(uint32_t layers=2) {
 assert(layers==1 || layers==2);
 reset();
 update_count=fail_update_a=fail_update_b=eye_queries=0;
 profile_create_count=destroyed_profile_count=0;
 for(int i=0;i<16;++i) {
  profile_create_observations[i]=ProfileCreateObservation();
  profile_create_results[i]=XR_SUCCESS;
  profile_create_outputs[i]=XR_NULL_HANDLE;
  destroyed_profiles[i]=XR_NULL_HANDLE;
 }
 g.useVulkan=true;g.session=session(99);g.sessionState=XR_SESSION_STATE_FOCUSED;
 g.initialized=g.sessionRunning=g.frameBegun=g.shouldRender=true;
 g.vk.densityMaps=true;g.vk.arrayLayers=layers;
 g.foveationSupported=true;g.foveationEyeSupported=true;
 g.xr.CreateFoveationProfile=create_profile;
 g.xr.DestroyFoveationProfile=destroy_profile;
 g.xr.UpdateSwapchain=update_foveation;
 g.xr.FoveationEyeTrackedState=query_eye;
 for(int i=0;i<2;++i) {
  g.chain[i].handle=swapchain(201+i);
  g.chain[i].acquired=g.chain[i].waited=true;
 }
}
static void assert_off_request(int index) {
 const ProfileCreateObservation &request=profile_create_observations[index];
 assert(request.root_type==XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB);
 assert(!request.root_has_next);
}
static void assert_level_request(int index, XrFoveationLevelFB level,
 XrFoveationDynamicFB dynamic, bool eye_tracked) {
 const ProfileCreateObservation &request=profile_create_observations[index];
 assert(request.root_type==XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB && request.root_has_next);
 assert(request.first_type==XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB);
 assert(request.level==level && request.vertical_offset==0.f && request.dynamic==dynamic);
 assert(request.first_has_next==eye_tracked);
 if(eye_tracked) {
  assert(request.second_type==XR_TYPE_FOVEATION_EYE_TRACKED_PROFILE_CREATE_INFO_META);
  assert(!request.second_has_next);
 }
}
static void destroy_profiles_and_expect(const int *create_indices, int count) {
 destroy_foveation_profiles();
 assert(destroyed_profile_count==count);
 for(int i=0;i<count;++i)
  assert(destroyed_profiles[i]==profile_create_outputs[create_indices[i]]);
 assert(!g.foveationOff && !g.foveationFixed && !g.foveationEye);
 assert(!g.foveationFixedAvailable && !g.foveationEyeAvailable);
}
static void test_create_foveation_profiles() {
 // Full supported profile set: the actual FB/META chains and initial off setter.
 prepare_profile_creation();
 assert(create_foveation_profiles());
 assert(profile_create_count==3 && g.foveationFixedAvailable && g.foveationEyeAvailable);
 assert(g.foveationOff==profile_create_outputs[0] && g.foveationFixed==profile_create_outputs[1]);
 assert(g.foveationEye==profile_create_outputs[2] && update_count==1);
 assert_off_request(0);
 assert_level_request(1,XR_FOVEATION_LEVEL_LOW_FB,XR_FOVEATION_DYNAMIC_DISABLED_FB,false);
 assert_level_request(2,XR_FOVEATION_LEVEL_HIGH_FB,XR_FOVEATION_DYNAMIC_LEVEL_ENABLED_FB,true);
 assert_update(0,swapchain(201),g.foveationOff);
 const int all_profiles[]={2,1,0};destroy_profiles_and_expect(all_profiles,3);

 // Optional dynamic eye rejection retries only the static eye-tracked profile.
 prepare_profile_creation();profile_create_results[2]=XR_ERROR_FEATURE_UNSUPPORTED;
 assert(create_foveation_profiles());
 assert(profile_create_count==4 && g.foveationEyeAvailable && g.foveationEye==profile_create_outputs[3]);
 assert(!g.terminal && update_count==1);
 assert_off_request(0);
 assert_level_request(1,XR_FOVEATION_LEVEL_LOW_FB,XR_FOVEATION_DYNAMIC_DISABLED_FB,false);
 assert_level_request(2,XR_FOVEATION_LEVEL_HIGH_FB,XR_FOVEATION_DYNAMIC_LEVEL_ENABLED_FB,true);
 assert_level_request(3,XR_FOVEATION_LEVEL_LOW_FB,XR_FOVEATION_DYNAMIC_DISABLED_FB,true);
 assert_update(0,swapchain(201),g.foveationOff);
 const int static_retry_profiles[]={3,1,0};destroy_profiles_and_expect(static_retry_profiles,3);

 // A failed optional fixed profile does not prevent a supported eye profile.
 prepare_profile_creation();profile_create_results[1]=XR_ERROR_FEATURE_UNSUPPORTED;
 assert(create_foveation_profiles());
 assert(profile_create_count==3 && !g.foveationFixedAvailable && !g.foveationFixed);
 assert(g.foveationEyeAvailable && g.foveationEye==profile_create_outputs[2]);
 assert_level_request(2,XR_FOVEATION_LEVEL_HIGH_FB,XR_FOVEATION_DYNAMIC_LEVEL_ENABLED_FB,true);
 assert_update(0,swapchain(201),g.foveationOff);
 const int fixed_failed_profiles[]={2,0};destroy_profiles_and_expect(fixed_failed_profiles,2);

 // Failed dynamic and static eye requests leave eyes unavailable; mode 2 stays off.
 prepare_profile_creation();
 profile_create_results[2]=XR_ERROR_FEATURE_UNSUPPORTED;
 profile_create_results[3]=XR_ERROR_RUNTIME_FAILURE;
 assert(create_foveation_profiles());
 assert(profile_create_count==4 && g.foveationFixedAvailable && !g.foveationEyeAvailable);
 assert(!g.foveationEye && update_count==1 && eye_queries==0);
 assert_level_request(2,XR_FOVEATION_LEVEL_HIGH_FB,XR_FOVEATION_DYNAMIC_LEVEL_ENABLED_FB,true);
 assert_level_request(3,XR_FOVEATION_LEVEL_LOW_FB,XR_FOVEATION_DYNAMIC_DISABLED_FB,true);
 assert(invoke(2,1)==0 && update_count==1 && eye_queries==0);
 assert_no_fixed_profile();
 const int eyes_unavailable_profiles[]={1,0};destroy_profiles_and_expect(eyes_unavailable_profiles,2);

 // The mandatory off profile failure aborts before fixed/eye creation or updates.
 prepare_profile_creation();profile_create_results[0]=XR_ERROR_RUNTIME_FAILURE;
 assert(!create_foveation_profiles());
 assert(profile_create_count==1 && update_count==0 && !g.terminal);
 assert(!g.foveationOff && !g.foveationFixed && !g.foveationEye);
 destroy_profiles_and_expect(0,0); // even the fake non-null error output is not owned

 // Loss-pending during dynamic eye creation is terminal, suppresses static retry,
 // and retains its success-class returned handle for ordered teardown.
 prepare_profile_creation();profile_create_results[2]=XR_SESSION_LOSS_PENDING;
 assert(!create_foveation_profiles());
 assert(profile_create_count==3 && g.terminal && g.stopReason==VRXR_STOP_SESSION_LOST);
 assert(g.foveationEye==profile_create_outputs[2] && !g.foveationEyeAvailable);
 assert(g.foveationFixedAvailable && update_count==0);
 assert_level_request(2,XR_FOVEATION_LEVEL_HIGH_FB,XR_FOVEATION_DYNAMIC_LEVEL_ENABLED_FB,true);
 const int loss_pending_profiles[]={2,1,0};destroy_profiles_and_expect(loss_pending_profiles,3);

 // Off initialization failure aborts after the second swapchain setter and keeps
 // every successfully created profile available to the existing cleanup owner.
 prepare_profile_creation(1);fail_update_a=2;
 assert(!create_foveation_profiles());
 assert(profile_create_count==3 && update_count==2 && !g.terminal);
 assert_update(0,swapchain(201),g.foveationOff);
 assert_update(1,swapchain(202),g.foveationOff);
 assert(g.chain[0].lastFoveationProfile==g.foveationOff);
 assert(!g.chain[1].lastFoveationProfile);
 const int off_setter_failure_profiles[]={2,1,0};destroy_profiles_and_expect(off_setter_failure_profiles,3);
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

 test_create_foveation_profiles();
 puts("OpenXR foveation prepared-dispatch and profile-creation fixture passed; borrowed images, KHR GPU protection, and FB/META runtime/gaze/device behavior remain unproven");
 return 0;
}

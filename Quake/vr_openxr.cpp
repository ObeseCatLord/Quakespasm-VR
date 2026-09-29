/* Native OpenXR backend. This translation unit has no engine includes:
 * gameplay and graphics-device ownership remain with the engine. */
#define XR_NO_PROTOTYPES
#define XR_USE_GRAPHICS_API_VULKAN
#if defined(_WIN32)
#define XR_USE_PLATFORM_WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h>
#endif
#ifdef USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif
#include "vr_openxr_vulkan.h"
#include "vr_openxr.h"
#include "vr_foveation_stability.h"
#include "sha256.h"
#include <string>
#include "thirdparty/openxr/openxr_platform.h"
#include "thirdparty/openxr/XR_MNDX_xdev_space.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
#ifdef USE_SDL3
typedef SDL_SharedObject *LoaderHandle;
#else
typedef void *LoaderHandle;
#endif
enum { kHands = 2, kViews = 2, kTrackerFirst = 3,
       kWaitNs = 100000000, kImageWaitAttempts = 50 };
/* Kept across runtime teardown so retained engine consumers can distinguish
 * a fresh pose sample from another read of the previous completed frame. */
static uint64_t g_sample_id;
enum ActionId {
	ACT_GRIP_POSE, ACT_HAPTIC, ACT_TRIGGER, ACT_GRIP, ACT_STICK, ACT_PAD,
	ACT_TRIGGER_CLICK, ACT_GRIP_CLICK, ACT_STICK_CLICK, ACT_PAD_CLICK,
	ACT_PRIMARY, ACT_SECONDARY, ACT_MENU, ACT_TRIGGER_TOUCH, ACT_GRIP_TOUCH,
	ACT_STICK_TOUCH, ACT_PAD_TOUCH, ACT_COUNT
};

struct Api {
	PFN_xrGetInstanceProcAddr gpa;
	PFN_xrCreateInstance CreateInstance;
	PFN_xrDestroyInstance DestroyInstance;
	PFN_xrEnumerateInstanceExtensionProperties EnumerateExtensions;
	PFN_xrGetInstanceProperties GetInstanceProperties;
	PFN_xrResultToString ResultToString;
	PFN_xrGetSystem GetSystem;
	PFN_xrGetSystemProperties GetSystemProperties;
	PFN_xrCreateSession CreateSession;
	PFN_xrDestroySession DestroySession;
	PFN_xrPollEvent PollEvent;
	PFN_xrBeginSession BeginSession;
	PFN_xrEndSession EndSession;
	PFN_xrCreateReferenceSpace CreateReferenceSpace;
	PFN_xrDestroySpace DestroySpace;
	PFN_xrLocateSpace LocateSpace;
	PFN_xrEnumerateViewConfigurationViews EnumerateViewConfigurationViews;
	PFN_xrEnumerateSwapchainFormats EnumerateSwapchainFormats;
	PFN_xrCreateSwapchain CreateSwapchain;
	PFN_xrDestroySwapchain DestroySwapchain;
	PFN_xrEnumerateSwapchainImages EnumerateSwapchainImages;
	PFN_xrAcquireSwapchainImage AcquireSwapchainImage;
	PFN_xrWaitSwapchainImage WaitSwapchainImage;
	PFN_xrReleaseSwapchainImage ReleaseSwapchainImage;
	PFN_xrWaitFrame WaitFrame;
	PFN_xrBeginFrame BeginFrame;
	PFN_xrEndFrame EndFrame;
	PFN_xrLocateViews LocateViews;
	PFN_xrStringToPath StringToPath;
	PFN_xrPathToString PathToString;
	PFN_xrCreateActionSet CreateActionSet;
	PFN_xrDestroyActionSet DestroyActionSet;
	PFN_xrCreateAction CreateAction;
	PFN_xrSuggestInteractionProfileBindings SuggestBindings;
	PFN_xrAttachSessionActionSets AttachActionSets;
	PFN_xrGetCurrentInteractionProfile CurrentProfile;
	PFN_xrCreateActionSpace CreateActionSpace;
	PFN_xrSyncActions SyncActions;
	PFN_xrGetActionStateBoolean BooleanState;
	PFN_xrGetActionStateFloat FloatState;
	PFN_xrGetActionStateVector2f VectorState;
	PFN_xrGetActionStatePose PoseState;
	PFN_xrApplyHapticFeedback Haptic;
	PFN_xrGetVulkanGraphicsRequirements2KHR VulkanRequirements;
	PFN_xrCreateVulkanInstanceKHR CreateVulkanInstance;
	PFN_xrGetVulkanGraphicsDevice2KHR VulkanGraphicsDevice;
	PFN_xrCreateVulkanDeviceKHR CreateVulkanDevice;
	PFN_xrUpdateSwapchainFB UpdateSwapchain;
	PFN_xrCreateFoveationProfileFB CreateFoveationProfile;
	PFN_xrDestroyFoveationProfileFB DestroyFoveationProfile;
	PFN_xrGetFoveationEyeTrackedStateMETA FoveationEyeTrackedState;
	PFN_xrGetVisibilityMaskKHR VisibilityMask;
	PFN_xrEnumerateViveTrackerPathsHTCX EnumerateTrackers;
	PFN_xrCreateXDevListMNDX CreateXDevList;
	PFN_xrGetXDevListGenerationNumberMNDX XDevGeneration;
	PFN_xrEnumerateXDevsMNDX EnumerateXDevs;
	PFN_xrGetXDevPropertiesMNDX XDevProperties;
	PFN_xrDestroyXDevListMNDX DestroyXDevList;
	PFN_xrCreateXDevSpaceMNDX CreateXDevSpace;
};

struct Chain {
	XrSwapchain handle;
	std::vector<XrSwapchainImageVulkan2KHR> vulkanImages;
	std::vector<XrSwapchainImageFoveationVulkanFB> densityImages;
	uint32_t index;
	uint32_t width, height;
	bool acquired, waited;
	uint32_t copiedMask;
	Chain() : handle(XR_NULL_HANDLE), index(0), width(0), height(0),
		acquired(false), waited(false), copiedMask(0) {}
};

struct Tracker { XrSpace space; char serial[256]; XrPath persistent, subaction; bool available; };
struct TrackerSource { XrPath path; XrSpace space; bool persistent; };
struct VisibilityMesh {
	std::vector<float> triangles;
	bool dirty;
	VisibilityMesh() : dirty(true) {}
};
struct VulkanQueue { uint32_t family, count; VkDeviceQueueCreateFlags flags; };
struct VulkanBinding {
	PFN_vkGetInstanceProcAddr getProc;
	VkInstance instance;
	VkPhysicalDevice physicalDevice;
	VkDevice device;
	uint32_t apiVersion;
	XrGraphicsRequirementsVulkan2KHR requirements;
	std::vector<VulkanQueue> queues;
	VkFormat format;
	XrSwapchainUsageFlags extraUsage;
	bool optionalTransferSourceUnsupported;
	uint32_t arrayLayers;
	bool densityMaps;
	bool fragmentDensityMapEnabled;
	VkImageCreateFlags densityImageFlags;
	void (*retireImages)(void *);
	void *owner;
	void (*lockQueue)(void *);
	void (*unlockQueue)(void *);
	void *queueOwner;
	VulkanBinding() : getProc(0), instance(VK_NULL_HANDLE), physicalDevice(VK_NULL_HANDLE),
		device(VK_NULL_HANDLE), apiVersion(0), requirements(), format(VK_FORMAT_UNDEFINED), extraUsage(0), optionalTransferSourceUnsupported(false), arrayLayers(1), densityMaps(false), fragmentDensityMapEnabled(false), densityImageFlags(0), retireImages(0), owner(0), lockQueue(0), unlockQueue(0), queueOwner(0) {}
};
struct State {
	LoaderHandle loader;
	void (*log)(const char *);
	Api xr;
	bool useVulkan, localFloorSupported;
	VulkanBinding vk;
	XrInstance instance;
	XrSystemId system;
	XrSession session;
	XrSpace appSpace, viewSpace, handSpace[kHands], gazeSpace;
	XrActionSet actions, gazeActions;
	XrAction action[ACT_COUNT];
	XrAction gazeAction, trackerAction;
	XrPath handPath[kHands];
	Chain chain[kViews];
	VisibilityMesh masks[kViews];
	std::vector<Tracker> trackers;
	std::vector<TrackerSource> trackerSources;
	uint32_t trackerVersion;
	bool htcxSupported, trackersDirty;
	XrXDevListMNDX xdevList;
	XrView views[kViews];
	XrFrameState frameState;
	XrSessionState sessionState;
	XrReferenceSpaceType appSpaceType, pendingReferenceType;
	XrTime pendingReferenceTime;
	XrEnvironmentBlendMode blend;
	bool initialized, sessionRunning, terminal, frameBegun, shouldRender;
	vrxr_stop_reason_t stopReason;
	bool discoveredGaze, discoveredHtcx, discoveredXdev;
	bool foveationSupported, foveationEyeSupported;
	bool vulkanSwapchainImageFlagsSupported;
	bool foveationFixedAvailable, foveationEyeAvailable;
	XrFoveationProfileFB foveationOff, foveationFixed, foveationEye;
	vrf_policy_state_t foveationEyePolicy;
	bool gazeSupported, gazeEnabled, trackerEnabled, xdevSupported, frameSupported, maskSupported, referenceChanged, referencePending;
	char runtime[XR_MAX_RUNTIME_NAME_SIZE];
	char systemName[XR_MAX_SYSTEM_NAME_SIZE];
	State() : loader(0), log(0), xr(), useVulkan(false), localFloorSupported(false), instance(XR_NULL_HANDLE), system(0), session(XR_NULL_HANDLE),
		appSpace(XR_NULL_HANDLE), viewSpace(XR_NULL_HANDLE), handSpace(), gazeSpace(XR_NULL_HANDLE), actions(XR_NULL_HANDLE), gazeActions(XR_NULL_HANDLE), action(), gazeAction(XR_NULL_HANDLE), trackerAction(XR_NULL_HANDLE),
		handPath(), chain(), trackers(), trackerSources(), trackerVersion(0), htcxSupported(false), trackersDirty(false), xdevList(XR_NULL_HANDLE), views(), frameState(),
		sessionState(XR_SESSION_STATE_IDLE), appSpaceType(XR_REFERENCE_SPACE_TYPE_LOCAL), pendingReferenceType(XR_REFERENCE_SPACE_TYPE_LOCAL),
		pendingReferenceTime(0), blend(XR_ENVIRONMENT_BLEND_MODE_OPAQUE), initialized(false), sessionRunning(false), terminal(false),
		frameBegun(false), shouldRender(false), stopReason(VRXR_STOP_NONE), discoveredGaze(false), discoveredHtcx(false), discoveredXdev(false), foveationSupported(false), foveationEyeSupported(false), vulkanSwapchainImageFlagsSupported(false), foveationFixedAvailable(false), foveationEyeAvailable(false), foveationOff(XR_NULL_HANDLE), foveationFixed(XR_NULL_HANDLE), foveationEye(XR_NULL_HANDLE), foveationEyePolicy(), gazeSupported(false), gazeEnabled(false), trackerEnabled(false), xdevSupported(false), frameSupported(false), maskSupported(false),
		referenceChanged(false), referencePending(false), runtime(), systemName() {}
};
static State g;

struct VulkanQueueLock {
	bool locked;
	VulkanQueueLock() : locked(false) {
		if(g.useVulkan && g.session && g.vk.lockQueue && g.vk.unlockQueue) {
			g.vk.lockQueue(g.vk.queueOwner);
			locked=true;
		}
	}
	~VulkanQueueLock() {
		if(locked) g.vk.unlockQueue(g.vk.queueOwner);
	}
};

/* Views and swapchain owners are distinct: Vulkan multiview stores both views
 * in one array image. Keep acquire/wait/release on the original Chain owner. */
static uint32_t swapchain_layers() {
	if (g.useVulkan) return g.vk.arrayLayers;
	return 1;
}
static int swapchain_count() { return swapchain_layers()==kViews ? 1 : kViews; }
static Chain &eye_chain(int eye) { return g.chain[swapchain_layers()==kViews ? 0 : eye]; }

static void say(const char *message) { if (g.log) g.log(message); }
static void sayf(const char *where, XrResult result) {
	// Keep the most specific loss reason even if later cleanup calls also fail.
	if(result==XR_ERROR_INSTANCE_LOST) { g.stopReason=VRXR_STOP_INSTANCE_LOST; g.terminal=true; }
	else if(result==XR_SESSION_LOSS_PENDING || result==XR_ERROR_SESSION_LOST) {
		if(g.stopReason<VRXR_STOP_SESSION_LOST) g.stopReason=VRXR_STOP_SESSION_LOST;
		g.terminal=true;
	}
	char resultText[XR_MAX_RESULT_STRING_SIZE] = {0};
	if (g.xr.ResultToString && g.instance != XR_NULL_HANDLE)
		g.xr.ResultToString(g.instance, result, resultText);
	char line[512];
	std::snprintf(line, sizeof(line), "OpenXR: %s: %s (%d)", where,
		resultText[0] ? resultText : "OpenXR error", (int)result);
	say(line);
}
static bool ok(const char *where, XrResult result) {
	if (result == XR_SUCCESS) return true;
	sayf(where, result);
	return false;
}
template <typename T> static bool proc(XrInstance instance, const char *name, T *out, bool required = true) {
	PFN_xrVoidFunction raw = 0;
	XrResult result = g.xr.gpa(instance, name, &raw);
	if (result == XR_SUCCESS && raw) { *out = reinterpret_cast<T>(raw); return true; }
	if (required) sayf(name, result);
	*out = 0;
	return false;
}
static bool extension(const std::vector<XrExtensionProperties> &extensions, const char *name) {
	for (size_t i = 0; i < extensions.size(); ++i)
		if (!std::strcmp(extensions[i].extensionName, name)) return true;
	return false;
}
static bool extension_version(const std::vector<XrExtensionProperties> &extensions, const char *name, uint32_t version) {
	for (size_t i = 0; i < extensions.size(); ++i) {
		if (std::strcmp(extensions[i].extensionName, name)) continue;
		if (extensions[i].extensionVersion == version) return true;
		say("OpenXR: MNDX xdev preview revision differs from vendored ABI; tracker support disabled");
		return false;
	}
	return false;
}
static XrPosef identity_pose() {
	XrPosef p; std::memset(&p, 0, sizeof(p)); p.orientation.w = 1.f; return p;
}
static void matrix(const XrPosef &p, float out[3][4]) {
	const float x=p.orientation.x, y=p.orientation.y, z=p.orientation.z, w=p.orientation.w;
	out[0][0]=1-2*y*y-2*z*z; out[0][1]=2*x*y-2*z*w; out[0][2]=2*x*z+2*y*w; out[0][3]=p.position.x;
	out[1][0]=2*x*y+2*z*w; out[1][1]=1-2*x*x-2*z*z; out[1][2]=2*y*z-2*x*w; out[1][3]=p.position.y;
	out[2][0]=2*x*z-2*y*w; out[2][1]=2*y*z+2*x*w; out[2][2]=1-2*x*x-2*y*y; out[2][3]=p.position.z;
}
static void device_from_space(vrxr_device_t *device, XrSpace space, int kind, int hand) {
	std::memset(device, 0, sizeof(*device));
	device->kind = kind; device->hand = hand;
	if (space == XR_NULL_HANDLE || !g.frameState.predictedDisplayTime) return;
	XrSpaceVelocity velocity = { XR_TYPE_SPACE_VELOCITY };
	XrSpaceLocation location = { XR_TYPE_SPACE_LOCATION, &velocity };
	if (!ok("xrLocateSpace", g.xr.LocateSpace(space, g.appSpace, g.frameState.predictedDisplayTime, &location))) return;
	device->valid = (location.locationFlags & (XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT)) ==
		(XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT);
	device->tracked = (location.locationFlags & (XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT)) ==
		(XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT);
	device->connected = device->valid;
	if (device->valid) matrix(location.pose, device->matrix);
	if (velocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) {
		device->velocity_valid = 1; device->velocity[0]=velocity.linearVelocity.x; device->velocity[1]=velocity.linearVelocity.y; device->velocity[2]=velocity.linearVelocity.z;
	}
	if (velocity.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) {
		device->angular_velocity_valid = 1; device->angular_velocity[0]=velocity.angularVelocity.x; device->angular_velocity[1]=velocity.angularVelocity.y; device->angular_velocity[2]=velocity.angularVelocity.z;
	}
}
static bool bool_action(ActionId action, int hand, bool *active) {
	XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
	XrActionStateBoolean state = { XR_TYPE_ACTION_STATE_BOOLEAN };
	info.action=g.action[action]; info.subactionPath=g.handPath[hand];
	if (!ok("xrGetActionStateBoolean", g.xr.BooleanState(g.session, &info, &state))) { *active=false; return false; }
	*active=state.isActive == XR_TRUE; return *active && state.currentState == XR_TRUE;
}
static float float_action(ActionId action, int hand, bool *active) {
	XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
	XrActionStateFloat state = { XR_TYPE_ACTION_STATE_FLOAT };
	info.action=g.action[action]; info.subactionPath=g.handPath[hand];
	if (!ok("xrGetActionStateFloat", g.xr.FloatState(g.session, &info, &state))) { *active=false; return 0.f; }
	*active=state.isActive == XR_TRUE; return *active ? state.currentState : 0.f;
}
static void vector_action(ActionId action, int hand, float out[2], bool *active) {
	XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
	XrActionStateVector2f state = { XR_TYPE_ACTION_STATE_VECTOR2F };
	info.action=g.action[action]; info.subactionPath=g.handPath[hand];
	if (!ok("xrGetActionStateVector2f", g.xr.VectorState(g.session, &info, &state))) { *active=false; return; }
	*active=state.isActive == XR_TRUE; out[0]=*active ? state.currentState.x : 0.f; out[1]=*active ? state.currentState.y : 0.f;
}
static int profile_for_hand(int hand) {
	XrInteractionProfileState profile = { XR_TYPE_INTERACTION_PROFILE_STATE };
	if (!ok("xrGetCurrentInteractionProfile", g.xr.CurrentProfile(g.session, g.handPath[hand], &profile)) || profile.interactionProfile == XR_NULL_PATH) return VRXR_PROFILE_SIMPLE;
	uint32_t size=0; char path[XR_MAX_PATH_LENGTH] = {0};
	if (!ok("xrPathToString", g.xr.PathToString(g.instance, profile.interactionProfile, sizeof(path), &size, path))) return VRXR_PROFILE_SIMPLE;
	if (std::strstr(path, "index_controller")) return VRXR_PROFILE_INDEX;
	if (std::strstr(path, "vive_controller")) return VRXR_PROFILE_VIVE;
	if (std::strstr(path, "touch_controller")) return VRXR_PROFILE_TOUCH;
	if (std::strstr(path, "frame_controller_valve")) return VRXR_PROFILE_FRAME;
	return VRXR_PROFILE_SIMPLE;
}
static void input_for_hand(vrxr_input_t *input, int hand) {
	std::memset(input, 0, sizeof(*input)); input->profile=profile_for_hand(hand);
	bool active=false, any=false;
	input->trigger=float_action(ACT_TRIGGER, hand, &active); any |= active;
	input->grip=float_action(ACT_GRIP, hand, &active); any |= active;
	vector_action(ACT_STICK, hand, input->stick, &active); any |= active;
	vector_action(ACT_PAD, hand, input->pad, &active); any |= active;
	struct Button { ActionId action; uint32_t bit; bool touch; } buttons[] = {
		{ACT_TRIGGER_CLICK,VRXR_BUTTON_TRIGGER,false},{ACT_GRIP_CLICK,VRXR_BUTTON_GRIP,false},
		{ACT_STICK_CLICK,VRXR_BUTTON_STICK,false},{ACT_PAD_CLICK,VRXR_BUTTON_PAD,false},
		{ACT_PRIMARY,VRXR_BUTTON_PRIMARY,false},{ACT_SECONDARY,VRXR_BUTTON_SECONDARY,false},
		{ACT_MENU,VRXR_BUTTON_MENU,false},{ACT_TRIGGER_TOUCH,VRXR_BUTTON_TRIGGER,true},
		{ACT_GRIP_TOUCH,VRXR_BUTTON_GRIP,true},{ACT_STICK_TOUCH,VRXR_BUTTON_STICK,true},
		{ACT_PAD_TOUCH,VRXR_BUTTON_PAD,true}
	};
	for (size_t i=0; i<sizeof(buttons)/sizeof(buttons[0]); ++i) {
		bool buttonActive=false, down=bool_action(buttons[i].action, hand, &buttonActive);
		any |= buttonActive;
		if (down) { if (buttons[i].touch) input->touched |= buttons[i].bit; else input->pressed |= buttons[i].bit; }
	}
	input->active=any ? 1 : 0;
}
static bool path(const char *text, XrPath *out) { return ok("xrStringToPath", g.xr.StringToPath(g.instance, text, out)); }
struct Binding { ActionId action; const char *path; };
static void suggest(const char *profile, const Binding *bindings, size_t count) {
	XrPath profilePath;
	if (!path(profile, &profilePath)) return;
	std::vector<XrActionSuggestedBinding> suggested; suggested.reserve(count);
	for (size_t i=0; i<count; ++i) { XrPath binding; if (path(bindings[i].path, &binding)) { XrActionSuggestedBinding b={g.action[bindings[i].action],binding}; suggested.push_back(b); } }
	XrInteractionProfileSuggestedBinding info = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
	info.interactionProfile=profilePath; info.countSuggestedBindings=(uint32_t)suggested.size(); info.suggestedBindings=suggested.data();
	if (!suggested.empty()) ok("xrSuggestInteractionProfileBindings", g.xr.SuggestBindings(g.instance, &info));
}
static bool create_action(ActionId id, XrActionType type, const char *name, bool handScoped) {
	XrActionCreateInfo info = { XR_TYPE_ACTION_CREATE_INFO };
	info.actionType=type; std::strncpy(info.actionName,name,sizeof(info.actionName)-1); std::strncpy(info.localizedActionName,name,sizeof(info.localizedActionName)-1);
	if (handScoped) { info.countSubactionPaths=kHands; info.subactionPaths=g.handPath; }
	return ok("xrCreateAction", g.xr.CreateAction(g.actions, &info, &g.action[id]));
}
/* Opaque runtime paths are not OpenVR serials. Keep their entire identity in
 * the digest; base64url fits all 256 bits plus the source namespace in the
 * existing 63-character FBT profile field. No profile format change. */
static void tracker_identity(const char *source, const char *identity, char out[256]) {
	out[0]=0;
	if (!identity || !*identity) return;
	if (!std::strcmp(source,"xrmndx")) {
		const size_t length=std::strlen(identity);
		bool safe=length<64;
		for(size_t i=0;i<length && safe;++i) {
			const unsigned char c=(unsigned char)identity[i];
			safe=(c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='.' || c=='_' || c==':' || c=='-';
		}
		if(safe) { std::strcpy(out,identity); return; }
	}
	qs_sha256_t state; uint8_t digest[32];
	QS_SHA256Init(&state); QS_SHA256Update(&state,source,std::strlen(source)+1);
	QS_SHA256Update(&state,identity,std::strlen(identity)); QS_SHA256Final(&state,digest);
	static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	std::snprintf(out,256,"%s:",source); size_t offset=std::strlen(out);
	unsigned bits=0, value=0;
	for(size_t i=0;i<sizeof(digest);++i) {
		value=(value<<8)|digest[i]; bits+=8;
		while(bits>=6) { bits-=6; out[offset++]=alphabet[(value>>bits)&63]; }
	}
	if(bits) out[offset++]=alphabet[(value<<(6-bits))&63];
	out[offset]=0;
}

static bool tracker_path_string(XrPath value, std::string &text) {
	char buffer[XR_MAX_PATH_LENGTH]={}; uint32_t count=0;
	if(value==XR_NULL_PATH || !ok("xrPathToString tracker",g.xr.PathToString(g.instance,value,sizeof(buffer),&count,buffer)) ||
	   count<2 || count>sizeof(buffer) || buffer[count-1]!=0 || std::strlen(buffer)+1!=count) return false;
	text.assign(buffer,count-1); return true;
}
static bool enumerate_htcx(std::vector<XrViveTrackerPathsHTCX> &paths) {
	/* A connection may change the size between the two calls. Never consume
	 * partial data or preserve old role-to-hardware mappings after failure. */
	for(int attempt=0;attempt<3;++attempt) {
		uint32_t count=0;
		if(!ok("xrEnumerateViveTrackerPathsHTCX",g.xr.EnumerateTrackers(g.instance,0,&count,0)) || count>4096) return false;
		paths.assign(count,XrViveTrackerPathsHTCX{XR_TYPE_VIVE_TRACKER_PATHS_HTCX});
		if(!count) return true;
		const uint32_t capacity=count;
		XrResult result=g.xr.EnumerateTrackers(g.instance,capacity,&count,paths.data());
		if(result==XR_ERROR_SIZE_INSUFFICIENT) continue;
		if(!ok("xrEnumerateViveTrackerPathsHTCX",result) || count>capacity) return false;
		paths.resize(count); return true;
	}
	return false;
}
static void refresh_htcx_trackers() {
	for(size_t i=0;i<g.trackers.size();++i) { g.trackers[i].available=false; g.trackers[i].space=XR_NULL_HANDLE; }
	std::vector<XrViveTrackerPathsHTCX> paths;
	if(!enumerate_htcx(paths)) return; // dirty remains set: retry without stale poses
	g.trackersDirty=false;
	for(size_t i=0;i<paths.size();++i) {
		const XrPath persistent=paths[i].persistentPath;
		if(persistent==XR_NULL_PATH) continue;
		bool duplicate=false;
		for(size_t j=0;j<paths.size();++j) if(i!=j && paths[j].persistentPath==persistent) duplicate=true;
		if(duplicate) continue;
		TrackerSource *source=0;
		for(size_t j=0;j<g.trackerSources.size();++j)
			if(g.trackerSources[j].persistent && g.trackerSources[j].path==persistent) source=&g.trackerSources[j];
		if(!source && paths[i].rolePath!=XR_NULL_PATH) {
			bool ambiguous=false;
			for(size_t j=0;j<paths.size();++j) if(i!=j && paths[j].rolePath==paths[i].rolePath) ambiguous=true;
			if(!ambiguous) for(size_t j=0;j<g.trackerSources.size();++j)
				if(!g.trackerSources[j].persistent && g.trackerSources[j].path==paths[i].rolePath) source=&g.trackerSources[j];
		}
		size_t slot=0;
		for(;slot<g.trackers.size();++slot) if(g.trackers[slot].persistent==persistent) break;
		if(slot==g.trackers.size()) {
			if(slot>=VRXR_MAX_DEVICES-kTrackerFirst) {
				/* Preserve live device slots regardless of enumeration order.
				 * Reclaim only an identity absent from this complete snapshot;
				 * the existing FBT serial binding rejects different hardware. */
				for(slot=0;slot<g.trackers.size();++slot) {
					bool present=false;
					for(size_t j=0;j<paths.size();++j) if(paths[j].persistentPath==g.trackers[slot].persistent) present=true;
					if(!present) break;
				}
				if(slot==g.trackers.size()) continue;
			}
			std::string identity; if(!tracker_path_string(persistent,identity)) continue;
			Tracker tracker={}; tracker.persistent=persistent;
			tracker_identity("xrhtcx",identity.c_str(),tracker.serial);
			if(slot==g.trackers.size()) g.trackers.push_back(tracker); else g.trackers[slot]=tracker;
			if(!source) say("OpenXR: new HTCX tracker has no unique supported role; assign a runtime role or restart VR to bind its persistent path");
		}
		Tracker &tracker=g.trackers[slot];
		if(source && source->space) { tracker.space=source->space; tracker.subaction=source->path; tracker.available=true; }
	}
}
static bool create_htcx_actions() {
	static const char *roles[]={"handheld_object","left_foot","right_foot","left_shoulder","right_shoulder",
		"left_elbow","right_elbow","left_knee","right_knee","waist","chest","camera","keyboard",
		"left_wrist","right_wrist","left_ankle","right_ankle"};
	const size_t roleCount=g.trackerVersion>=3 ? sizeof(roles)/sizeof(roles[0]) : 13;
	for(size_t i=0;i<roleCount;++i) {
		char text[128]; std::snprintf(text,sizeof(text),"/user/vive_tracker_htcx/role/%s",roles[i]);
		TrackerSource source={}; if(!path(text,&source.path)) return false;
		g.trackerSources.push_back(source);
	}
	std::vector<XrViveTrackerPathsHTCX> paths;
	if(enumerate_htcx(paths)) for(size_t i=0;i<paths.size() && g.trackerSources.size()<roleCount+VRXR_MAX_DEVICES-kTrackerFirst;++i) {
		if(paths[i].persistentPath==XR_NULL_PATH) continue;
		bool duplicate=false; for(size_t j=0;j<g.trackerSources.size();++j) if(g.trackerSources[j].path==paths[i].persistentPath) duplicate=true;
		if(duplicate) continue;
		std::string text;
		if(!tracker_path_string(paths[i].persistentPath,text) || text.size()+sizeof("/input/grip/pose")>XR_MAX_PATH_LENGTH) continue;
		TrackerSource source={}; source.path=paths[i].persistentPath; source.persistent=true; g.trackerSources.push_back(source);
	}
	std::vector<XrPath> subactions; std::vector<XrActionSuggestedBinding> bindings;
	for(size_t i=0;i<g.trackerSources.size();++i) subactions.push_back(g.trackerSources[i].path);
	XrActionCreateInfo info={XR_TYPE_ACTION_CREATE_INFO}; info.actionType=XR_ACTION_TYPE_POSE_INPUT;
	std::strcpy(info.actionName,"tracker_pose"); std::strcpy(info.localizedActionName,"Tracker pose");
	info.countSubactionPaths=(uint32_t)subactions.size(); info.subactionPaths=subactions.data();
	if(!ok("xrCreateAction tracker",g.xr.CreateAction(g.actions,&info,&g.trackerAction))) return false;
	for(size_t i=0;i<g.trackerSources.size();++i) {
		std::string text; XrPath binding;
		if(!tracker_path_string(g.trackerSources[i].path,text) || !path((text+"/input/grip/pose").c_str(),&binding)) return false;
		XrActionSuggestedBinding item={g.trackerAction,binding}; bindings.push_back(item);
	}
	XrPath profile; if(!path("/interaction_profiles/htc/vive_tracker_htcx",&profile)) return false;
	XrInteractionProfileSuggestedBinding suggestion={XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
	suggestion.interactionProfile=profile; suggestion.countSuggestedBindings=(uint32_t)bindings.size(); suggestion.suggestedBindings=bindings.data();
	return ok("xrSuggestInteractionProfileBindings tracker",g.xr.SuggestBindings(g.instance,&suggestion));
}
static void locate_trackers(vrxr_frame_t *frame, bool focused) {
	if(!g.trackerEnabled) return;
	if(g.htcxSupported && g.trackersDirty) refresh_htcx_trackers();
	for(size_t i=0;i<g.trackers.size();++i) {
		const Tracker &tracker=g.trackers[i]; vrxr_device_t *device=&frame->devices[kTrackerFirst+i];
		std::memset(device,0,sizeof(*device)); device->kind=VRXR_DEVICE_TRACKER; device->hand=-1;
		if(g.htcxSupported) {
			if(!focused || !tracker.available) continue;
			XrActionStateGetInfo info={XR_TYPE_ACTION_STATE_GET_INFO}; info.action=g.trackerAction; info.subactionPath=tracker.subaction;
			XrActionStatePose state={XR_TYPE_ACTION_STATE_POSE};
			if(!ok("xrGetActionStatePose tracker",g.xr.PoseState(g.session,&info,&state)) || !state.isActive) continue;
		}
		device_from_space(device,tracker.space,VRXR_DEVICE_TRACKER,-1);
		if(g.htcxSupported) device->connected=1; // active binding; pose validity is separate
		std::memcpy(device->serial,tracker.serial,sizeof(device->serial));
	}
}

static void disable_htcx_trackers() {
	for(size_t i=0;i<g.trackerSources.size();++i)
		if(g.trackerSources[i].space) g.xr.DestroySpace(g.trackerSources[i].space);
	g.trackerSources.clear(); g.trackerAction=XR_NULL_HANDLE;
	g.htcxSupported=false; g.trackersDirty=false;
}
/* Gaze is optional and has its own action set so disabling it also stops
 * runtime activation. Failures here must not remove head/controller input. */
static void disable_gaze_actions() {
	if (g.gazeSpace) g.xr.DestroySpace(g.gazeSpace);
	if (g.gazeActions) g.xr.DestroyActionSet(g.gazeActions);
	g.gazeSpace=XR_NULL_HANDLE; g.gazeAction=XR_NULL_HANDLE; g.gazeActions=XR_NULL_HANDLE;
	g.gazeSupported=false;
}
static bool create_gaze_actions() {
	XrActionSetCreateInfo set={XR_TYPE_ACTION_SET_CREATE_INFO};
	std::strncpy(set.actionSetName,"eye_tracking",sizeof(set.actionSetName)-1);
	std::strncpy(set.localizedActionSetName,"Eye tracking",sizeof(set.localizedActionSetName)-1);
	if (!ok("xrCreateActionSet gaze",g.xr.CreateActionSet(g.instance,&set,&g.gazeActions))) return false;
	XrActionCreateInfo action={XR_TYPE_ACTION_CREATE_INFO}; action.actionType=XR_ACTION_TYPE_POSE_INPUT;
	std::strncpy(action.actionName,"eye_gaze",sizeof(action.actionName)-1);
	std::strncpy(action.localizedActionName,"Eye gaze",sizeof(action.localizedActionName)-1);
	if (!ok("xrCreateAction eye_gaze",g.xr.CreateAction(g.gazeActions,&action,&g.gazeAction))) return false;
	XrPath profile,bindingPath;
	if (!path("/interaction_profiles/ext/eye_gaze_interaction",&profile) ||
	    !path("/user/eyes_ext/input/gaze_ext/pose",&bindingPath)) return false;
	XrActionSuggestedBinding binding={g.gazeAction,bindingPath};
	XrInteractionProfileSuggestedBinding bindings={XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
	bindings.interactionProfile=profile; bindings.countSuggestedBindings=1; bindings.suggestedBindings=&binding;
	if (!ok("xrSuggestInteractionProfileBindings gaze",g.xr.SuggestBindings(g.instance,&bindings))) return false;
	XrActionSpaceCreateInfo space={XR_TYPE_ACTION_SPACE_CREATE_INFO};
	space.action=g.gazeAction; space.poseInActionSpace=identity_pose();
	return ok("xrCreateActionSpace gaze",g.xr.CreateActionSpace(g.session,&space,&g.gazeSpace));
}
static bool create_actions() {
	if (!path("/user/hand/left", &g.handPath[0]) || !path("/user/hand/right", &g.handPath[1])) return false;
	XrActionSetCreateInfo set = { XR_TYPE_ACTION_SET_CREATE_INFO };
	std::strncpy(set.actionSetName,"gameplay",sizeof(set.actionSetName)-1); std::strncpy(set.localizedActionSetName,"Gameplay",sizeof(set.localizedActionSetName)-1);
	if (!ok("xrCreateActionSet",g.xr.CreateActionSet(g.instance,&set,&g.actions))) return false;
	struct Def { ActionId id; XrActionType type; const char *name; } defs[] = {
		{ACT_GRIP_POSE,XR_ACTION_TYPE_POSE_INPUT,"grip_pose"},{ACT_HAPTIC,XR_ACTION_TYPE_VIBRATION_OUTPUT,"haptic"},
		{ACT_TRIGGER,XR_ACTION_TYPE_FLOAT_INPUT,"trigger"},{ACT_GRIP,XR_ACTION_TYPE_FLOAT_INPUT,"grip"},
		{ACT_STICK,XR_ACTION_TYPE_VECTOR2F_INPUT,"stick"},{ACT_PAD,XR_ACTION_TYPE_VECTOR2F_INPUT,"pad"},
		{ACT_TRIGGER_CLICK,XR_ACTION_TYPE_BOOLEAN_INPUT,"trigger_click"},{ACT_GRIP_CLICK,XR_ACTION_TYPE_BOOLEAN_INPUT,"grip_click"},
		{ACT_STICK_CLICK,XR_ACTION_TYPE_BOOLEAN_INPUT,"stick_click"},{ACT_PAD_CLICK,XR_ACTION_TYPE_BOOLEAN_INPUT,"pad_click"},
		{ACT_PRIMARY,XR_ACTION_TYPE_BOOLEAN_INPUT,"primary"},{ACT_SECONDARY,XR_ACTION_TYPE_BOOLEAN_INPUT,"secondary"},{ACT_MENU,XR_ACTION_TYPE_BOOLEAN_INPUT,"menu"},
		{ACT_TRIGGER_TOUCH,XR_ACTION_TYPE_BOOLEAN_INPUT,"trigger_touch"},{ACT_GRIP_TOUCH,XR_ACTION_TYPE_BOOLEAN_INPUT,"grip_touch"},
		{ACT_STICK_TOUCH,XR_ACTION_TYPE_BOOLEAN_INPUT,"stick_touch"},{ACT_PAD_TOUCH,XR_ACTION_TYPE_BOOLEAN_INPUT,"pad_touch"}
	};
	for (size_t i=0;i<sizeof(defs)/sizeof(defs[0]);++i) if (!create_action(defs[i].id,defs[i].type,defs[i].name,true)) return false;
	Binding simple[]={{ACT_GRIP_POSE,"/user/hand/left/input/grip/pose"},{ACT_GRIP_POSE,"/user/hand/right/input/grip/pose"},{ACT_HAPTIC,"/user/hand/left/output/haptic"},{ACT_HAPTIC,"/user/hand/right/output/haptic"},{ACT_TRIGGER_CLICK,"/user/hand/left/input/select/click"},{ACT_TRIGGER_CLICK,"/user/hand/right/input/select/click"},{ACT_MENU,"/user/hand/left/input/menu/click"},{ACT_MENU,"/user/hand/right/input/menu/click"}};
	Binding touch[]={{ACT_GRIP_POSE,"/user/hand/left/input/grip/pose"},{ACT_GRIP_POSE,"/user/hand/right/input/grip/pose"},{ACT_HAPTIC,"/user/hand/left/output/haptic"},{ACT_HAPTIC,"/user/hand/right/output/haptic"},{ACT_TRIGGER,"/user/hand/left/input/trigger/value"},{ACT_TRIGGER,"/user/hand/right/input/trigger/value"},{ACT_GRIP,"/user/hand/left/input/squeeze/value"},{ACT_GRIP,"/user/hand/right/input/squeeze/value"},{ACT_GRIP_CLICK,"/user/hand/left/input/squeeze/value"},{ACT_GRIP_CLICK,"/user/hand/right/input/squeeze/value"},{ACT_STICK,"/user/hand/left/input/thumbstick"},{ACT_STICK,"/user/hand/right/input/thumbstick"},{ACT_STICK_CLICK,"/user/hand/left/input/thumbstick/click"},{ACT_STICK_CLICK,"/user/hand/right/input/thumbstick/click"},{ACT_PRIMARY,"/user/hand/left/input/x/click"},{ACT_PRIMARY,"/user/hand/right/input/a/click"},{ACT_SECONDARY,"/user/hand/left/input/y/click"},{ACT_SECONDARY,"/user/hand/right/input/b/click"},{ACT_MENU,"/user/hand/left/input/menu/click"},{ACT_TRIGGER_TOUCH,"/user/hand/left/input/trigger/touch"},{ACT_TRIGGER_TOUCH,"/user/hand/right/input/trigger/touch"},{ACT_STICK_TOUCH,"/user/hand/left/input/thumbstick/touch"},{ACT_STICK_TOUCH,"/user/hand/right/input/thumbstick/touch"}};
	Binding vive[]={{ACT_GRIP_POSE,"/user/hand/left/input/grip/pose"},{ACT_GRIP_POSE,"/user/hand/right/input/grip/pose"},{ACT_HAPTIC,"/user/hand/left/output/haptic"},{ACT_HAPTIC,"/user/hand/right/output/haptic"},{ACT_TRIGGER,"/user/hand/left/input/trigger/value"},{ACT_TRIGGER,"/user/hand/right/input/trigger/value"},{ACT_PAD,"/user/hand/left/input/trackpad"},{ACT_PAD,"/user/hand/right/input/trackpad"},{ACT_PAD_CLICK,"/user/hand/left/input/trackpad/click"},{ACT_PAD_CLICK,"/user/hand/right/input/trackpad/click"},{ACT_PAD_TOUCH,"/user/hand/left/input/trackpad/touch"},{ACT_PAD_TOUCH,"/user/hand/right/input/trackpad/touch"},{ACT_GRIP_CLICK,"/user/hand/left/input/squeeze/click"},{ACT_GRIP_CLICK,"/user/hand/right/input/squeeze/click"},{ACT_MENU,"/user/hand/left/input/menu/click"},{ACT_MENU,"/user/hand/right/input/menu/click"}};
	Binding index[]={{ACT_GRIP_POSE,"/user/hand/left/input/grip/pose"},{ACT_GRIP_POSE,"/user/hand/right/input/grip/pose"},{ACT_HAPTIC,"/user/hand/left/output/haptic"},{ACT_HAPTIC,"/user/hand/right/output/haptic"},{ACT_TRIGGER,"/user/hand/left/input/trigger/value"},{ACT_TRIGGER,"/user/hand/right/input/trigger/value"},{ACT_GRIP,"/user/hand/left/input/squeeze/value"},{ACT_GRIP,"/user/hand/right/input/squeeze/value"},{ACT_GRIP_CLICK,"/user/hand/left/input/squeeze/value"},{ACT_GRIP_CLICK,"/user/hand/right/input/squeeze/value"},{ACT_STICK,"/user/hand/left/input/thumbstick"},{ACT_STICK,"/user/hand/right/input/thumbstick"},{ACT_STICK_CLICK,"/user/hand/left/input/thumbstick/click"},{ACT_STICK_CLICK,"/user/hand/right/input/thumbstick/click"},{ACT_PRIMARY,"/user/hand/left/input/a/click"},{ACT_PRIMARY,"/user/hand/right/input/a/click"},{ACT_SECONDARY,"/user/hand/left/input/b/click"},{ACT_SECONDARY,"/user/hand/right/input/b/click"},{ACT_PAD,"/user/hand/left/input/trackpad"},{ACT_PAD,"/user/hand/right/input/trackpad"},{ACT_PAD_CLICK,"/user/hand/left/input/trackpad/force"},{ACT_PAD_CLICK,"/user/hand/right/input/trackpad/force"},{ACT_PAD_TOUCH,"/user/hand/left/input/trackpad/touch"},{ACT_PAD_TOUCH,"/user/hand/right/input/trackpad/touch"}};
	Binding frame[]={{ACT_GRIP_POSE,"/user/hand/left/input/grip/pose"},{ACT_GRIP_POSE,"/user/hand/right/input/grip/pose"},{ACT_HAPTIC,"/user/hand/left/output/haptic"},{ACT_HAPTIC,"/user/hand/right/output/haptic"},{ACT_TRIGGER,"/user/hand/left/input/trigger/value"},{ACT_TRIGGER,"/user/hand/right/input/trigger/value"},{ACT_GRIP,"/user/hand/left/input/squeeze/value"},{ACT_GRIP,"/user/hand/right/input/squeeze/value"},{ACT_GRIP_CLICK,"/user/hand/left/input/squeeze/value"},{ACT_GRIP_CLICK,"/user/hand/right/input/squeeze/value"},{ACT_STICK,"/user/hand/left/input/thumbstick"},{ACT_STICK,"/user/hand/right/input/thumbstick"},{ACT_STICK_CLICK,"/user/hand/left/input/thumbstick/click"},{ACT_STICK_CLICK,"/user/hand/right/input/thumbstick/click"},{ACT_PRIMARY,"/user/hand/left/input/dpad_left/click"},{ACT_PRIMARY,"/user/hand/right/input/a/click"},{ACT_SECONDARY,"/user/hand/left/input/dpad_right/click"},{ACT_SECONDARY,"/user/hand/right/input/b/click"},{ACT_MENU,"/user/hand/left/input/view/click"},{ACT_MENU,"/user/hand/right/input/menu/click"}};
	suggest("/interaction_profiles/khr/simple_controller",simple,sizeof(simple)/sizeof(simple[0]));
	suggest("/interaction_profiles/oculus/touch_controller",touch,sizeof(touch)/sizeof(touch[0]));
	suggest("/interaction_profiles/htc/vive_controller",vive,sizeof(vive)/sizeof(vive[0]));
	suggest("/interaction_profiles/valve/index_controller",index,sizeof(index)/sizeof(index[0]));
	if (g.gazeSupported && !create_gaze_actions()) {
		say("OpenXR: eye tracking unavailable; continuing without gaze input");
		disable_gaze_actions();
	}
	if (g.frameSupported) suggest("/interaction_profiles/valve/frame_controller_valve",frame,sizeof(frame)/sizeof(frame[0]));
	if(g.htcxSupported && !create_htcx_actions()) {
		say("OpenXR: HTCX action setup failed; tracker extension disabled");
		disable_htcx_trackers();
	}
	XrActionSet sets[]={g.actions,g.gazeActions};
	XrSessionActionSetsAttachInfo attach={XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO}; attach.countActionSets=g.gazeActions ? 2 : 1; attach.actionSets=sets;
	if (!ok("xrAttachSessionActionSets",g.xr.AttachActionSets(g.session,&attach))) return false;
	for(int hand=0;hand<kHands;++hand) { XrActionSpaceCreateInfo info={XR_TYPE_ACTION_SPACE_CREATE_INFO}; info.action=g.action[ACT_GRIP_POSE]; info.subactionPath=g.handPath[hand]; info.poseInActionSpace=identity_pose(); if(!ok("xrCreateActionSpace",g.xr.CreateActionSpace(g.session,&info,&g.handSpace[hand]))) return false; }
	if(g.htcxSupported) {
		for(size_t i=0;i<g.trackerSources.size();++i) {
			XrActionSpaceCreateInfo info={XR_TYPE_ACTION_SPACE_CREATE_INFO}; info.action=g.trackerAction;
			info.subactionPath=g.trackerSources[i].path; info.poseInActionSpace=identity_pose();
			if(!ok("xrCreateActionSpace tracker",g.xr.CreateActionSpace(g.session,&info,&g.trackerSources[i].space))) {
				say("OpenXR: HTCX space setup failed; tracker extension disabled");
				disable_htcx_trackers(); break;
			}
		}
		g.trackersDirty=g.htcxSupported;
	}
	return true;
}

static bool load_instance_functions() {
#define LOAD(member, name) if (!proc(g.instance, name, &g.xr.member)) return false
	LOAD(DestroyInstance,"xrDestroyInstance"); LOAD(GetInstanceProperties,"xrGetInstanceProperties"); LOAD(ResultToString,"xrResultToString");
	LOAD(GetSystem,"xrGetSystem"); LOAD(GetSystemProperties,"xrGetSystemProperties"); LOAD(CreateSession,"xrCreateSession"); LOAD(DestroySession,"xrDestroySession");
	LOAD(PollEvent,"xrPollEvent"); LOAD(BeginSession,"xrBeginSession"); LOAD(EndSession,"xrEndSession");
	LOAD(CreateReferenceSpace,"xrCreateReferenceSpace"); LOAD(DestroySpace,"xrDestroySpace"); LOAD(LocateSpace,"xrLocateSpace");
	LOAD(EnumerateViewConfigurationViews,"xrEnumerateViewConfigurationViews"); LOAD(EnumerateSwapchainFormats,"xrEnumerateSwapchainFormats");
	LOAD(CreateSwapchain,"xrCreateSwapchain"); LOAD(DestroySwapchain,"xrDestroySwapchain"); LOAD(EnumerateSwapchainImages,"xrEnumerateSwapchainImages");
	LOAD(AcquireSwapchainImage,"xrAcquireSwapchainImage"); LOAD(WaitSwapchainImage,"xrWaitSwapchainImage"); LOAD(ReleaseSwapchainImage,"xrReleaseSwapchainImage");
	LOAD(WaitFrame,"xrWaitFrame"); LOAD(BeginFrame,"xrBeginFrame"); LOAD(EndFrame,"xrEndFrame"); LOAD(LocateViews,"xrLocateViews");
	LOAD(StringToPath,"xrStringToPath"); LOAD(PathToString,"xrPathToString"); LOAD(CreateActionSet,"xrCreateActionSet"); LOAD(DestroyActionSet,"xrDestroyActionSet");
	LOAD(CreateAction,"xrCreateAction"); LOAD(SuggestBindings,"xrSuggestInteractionProfileBindings"); LOAD(AttachActionSets,"xrAttachSessionActionSets"); LOAD(CurrentProfile,"xrGetCurrentInteractionProfile");
	LOAD(CreateActionSpace,"xrCreateActionSpace"); LOAD(SyncActions,"xrSyncActions"); LOAD(BooleanState,"xrGetActionStateBoolean"); LOAD(FloatState,"xrGetActionStateFloat"); LOAD(VectorState,"xrGetActionStateVector2f"); LOAD(PoseState,"xrGetActionStatePose"); LOAD(Haptic,"xrApplyHapticFeedback");
	if(g.useVulkan) {
		LOAD(VulkanRequirements,"xrGetVulkanGraphicsRequirements2KHR");
		LOAD(CreateVulkanInstance,"xrCreateVulkanInstanceKHR");
		LOAD(VulkanGraphicsDevice,"xrGetVulkanGraphicsDevice2KHR");
		LOAD(CreateVulkanDevice,"xrCreateVulkanDeviceKHR");
		if(g.foveationSupported) {
			bool available=true;
			available=proc(g.instance,"xrUpdateSwapchainFB",&g.xr.UpdateSwapchain,false) && available;
			available=proc(g.instance,"xrCreateFoveationProfileFB",&g.xr.CreateFoveationProfile,false) && available;
			available=proc(g.instance,"xrDestroyFoveationProfileFB",&g.xr.DestroyFoveationProfile,false) && available;
			if(!available) {
				g.foveationSupported=false;
				g.foveationEyeSupported=false;
				say("OpenXR: FB Vulkan foveation functions unavailable; optimization disabled");
			}
		}
		if(g.foveationEyeSupported)
			g.foveationEyeSupported=proc(g.instance,"xrGetFoveationEyeTrackedStateMETA",&g.xr.FoveationEyeTrackedState,false);
	}
#undef LOAD
	if(g.htcxSupported) g.htcxSupported=proc(g.instance,"xrEnumerateViveTrackerPathsHTCX",&g.xr.EnumerateTrackers,false);
	if (g.maskSupported)
		g.maskSupported=proc(g.instance,"xrGetVisibilityMaskKHR",&g.xr.VisibilityMask,false);
	if (g.xdevSupported) {
		proc(g.instance,"xrCreateXDevListMNDX",&g.xr.CreateXDevList,false); proc(g.instance,"xrGetXDevListGenerationNumberMNDX",&g.xr.XDevGeneration,false);
		proc(g.instance,"xrEnumerateXDevsMNDX",&g.xr.EnumerateXDevs,false); proc(g.instance,"xrGetXDevPropertiesMNDX",&g.xr.XDevProperties,false);
		proc(g.instance,"xrDestroyXDevListMNDX",&g.xr.DestroyXDevList,false); proc(g.instance,"xrCreateXDevSpaceMNDX",&g.xr.CreateXDevSpace,false);
		g.xdevSupported = g.xr.CreateXDevList && g.xr.EnumerateXDevs && g.xr.XDevProperties && g.xr.DestroyXDevList && g.xr.CreateXDevSpace;
	}
	return true;
}

static bool create_space(XrReferenceSpaceType type, XrSpace *space) {
	XrReferenceSpaceCreateInfo info={XR_TYPE_REFERENCE_SPACE_CREATE_INFO}; info.referenceSpaceType=type; info.poseInReferenceSpace=identity_pose();
	return ok("xrCreateReferenceSpace",g.xr.CreateReferenceSpace(g.session,&info,space));
}
static bool create_swapchains() {
	uint32_t count=0; if (!ok("xrEnumerateViewConfigurationViews",g.xr.EnumerateViewConfigurationViews(g.instance,g.system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,0)) || count!=kViews) { say("OpenXR: runtime does not offer exactly two primary stereo views"); return false; }
	XrViewConfigurationView config[kViews]={{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}};
	if (!ok("xrEnumerateViewConfigurationViews",g.xr.EnumerateViewConfigurationViews(g.instance,g.system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,kViews,&count,config))) return false;
	uint32_t formats=0; if(!ok("xrEnumerateSwapchainFormats",g.xr.EnumerateSwapchainFormats(g.session,0,&formats,0)) || !formats) return false;
	std::vector<int64_t> available(formats); if(!ok("xrEnumerateSwapchainFormats",g.xr.EnumerateSwapchainFormats(g.session,formats,&formats,available.data()))) return false;
	int64_t selected=0;
	if(g.useVulkan) {
		/* The renderer encodes linear shader output through this sRGB target. */
		const int64_t preferred[]={VK_FORMAT_R8G8B8A8_SRGB,VK_FORMAT_B8G8R8A8_SRGB};
		for(size_t p=0;p<sizeof(preferred)/sizeof(preferred[0]) && !selected;++p)
			for(size_t i=0;i<available.size();++i) if(available[i]==preferred[p]) { selected=preferred[p]; break; }
		g.vk.format=(VkFormat)selected;
	}
	if (!selected) { say("OpenXR: runtime has no supported opaque sRGB color format"); return false; }
	for(int eye=0;eye<swapchain_count();++eye) {
		Chain &chain=g.chain[eye]; chain.width=config[eye].recommendedImageRectWidth; chain.height=config[eye].recommendedImageRectHeight;
		if (swapchain_layers()==kViews) {
			chain.width=std::max(config[0].recommendedImageRectWidth,config[1].recommendedImageRectWidth);
			chain.height=std::max(config[0].recommendedImageRectHeight,config[1].recommendedImageRectHeight);
		}
		XrSwapchainCreateInfo info={XR_TYPE_SWAPCHAIN_CREATE_INFO}; info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT; info.format=selected; info.sampleCount=1; info.width=chain.width; info.height=chain.height; info.faceCount=1; info.arraySize=swapchain_layers(); info.mipCount=1;
		XrSwapchainCreateInfoFoveationFB foveation={XR_TYPE_SWAPCHAIN_CREATE_INFO_FOVEATION_FB};
		XrVulkanSwapchainCreateInfoMETA imageFlags={XR_TYPE_VULKAN_SWAPCHAIN_CREATE_INFO_META};
		if(g.useVulkan) {
			info.usageFlags|=g.vk.extraUsage;
			/* FB's next is mutable while the enclosing create chain is const.
			 * Link this tail first, then prepend the const-next META structure. */
			if(g.vk.densityMaps) {
				foveation.flags=XR_SWAPCHAIN_CREATE_FOVEATION_FRAGMENT_DENSITY_MAP_BIT_FB;
				info.next=&foveation;
			}
			if(g.vk.densityImageFlags) {
				imageFlags.additionalCreateFlags=g.vk.densityImageFlags;
				imageFlags.additionalUsageFlags=0;
				imageFlags.next=info.next;
				info.next=&imageFlags;
			}
		}
		const XrResult createResult=g.xr.CreateSwapchain(g.session,&info,&chain.handle);
		if(createResult==XR_ERROR_FEATURE_UNSUPPORTED &&
		   (g.vk.extraUsage&XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT))
			g.vk.optionalTransferSourceUnsupported=true;
		if(!ok("xrCreateSwapchain",createResult)) return false;
		uint32_t imageCount=0; if(!ok("xrEnumerateSwapchainImages",g.xr.EnumerateSwapchainImages(chain.handle,0,&imageCount,0)) || !imageCount) return false;
		if(g.useVulkan) {
			chain.vulkanImages.resize(imageCount);
			if(g.vk.densityMaps) chain.densityImages.resize(imageCount);
			for(uint32_t i=0;i<imageCount;++i) {
				chain.vulkanImages[i].type=XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR;
				if(g.vk.densityMaps) {
					chain.densityImages[i].type=XR_TYPE_SWAPCHAIN_IMAGE_FOVEATION_VULKAN_FB;
					chain.vulkanImages[i].next=&chain.densityImages[i];
				}
			}
			if(!ok("xrEnumerateSwapchainImages",g.xr.EnumerateSwapchainImages(chain.handle,imageCount,&imageCount,
				reinterpret_cast<XrSwapchainImageBaseHeader*>(chain.vulkanImages.data())))) return false;
			if(g.vk.densityMaps) for(uint32_t i=0;i<imageCount;++i) {
				const XrSwapchainImageFoveationVulkanFB &density=chain.densityImages[i];
				if(!density.image || !density.width || !density.height) {
					say("OpenXR: runtime returned an incomplete fragment density map image");
					return false;
				}
			}
			continue;
		}
	}
	return true;
}

static bool update_foveation_profile(XrFoveationProfileFB profile) {
	if(!profile || !g.xr.UpdateSwapchain) return false;
	for(int eye=0;eye<swapchain_count();++eye) {
		XrSwapchainStateFoveationFB state={XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB};
		state.profile=profile;
		if(!ok("xrUpdateSwapchainFB",g.xr.UpdateSwapchain(g.chain[eye].handle,
			reinterpret_cast<const XrSwapchainStateBaseHeaderFB *>(&state)))) return false;
	}
	return true;
}
static bool create_foveation_profile(const char *label, const XrFoveationProfileCreateInfoFB &info,
                                     XrFoveationProfileFB &target) {
	XrFoveationProfileFB created=XR_NULL_HANDLE;
	XrResult result=g.xr.CreateFoveationProfile(g.session,&info,&created);
	// Error outputs are undefined. Loss-pending is a success result with a
	// valid child handle: retain it for teardown even though ok() stops setup.
	target=XR_SUCCEEDED(result) ? created : XR_NULL_HANDLE;
	return ok(label,result) && target!=XR_NULL_HANDLE;
}
static bool create_foveation_profiles() {
	if(!g.useVulkan || !g.vk.densityMaps) return true;
	if(!g.foveationSupported || !g.xr.CreateFoveationProfile || !g.xr.DestroyFoveationProfile || !g.xr.UpdateSwapchain) return false;
	XrFoveationProfileCreateInfoFB off={XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB};
	if(!create_foveation_profile("xrCreateFoveationProfileFB off",off,g.foveationOff)) return false;
	XrFoveationLevelProfileCreateInfoFB fixed={XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB};
	fixed.level=XR_FOVEATION_LEVEL_LOW_FB; fixed.verticalOffset=0.f; fixed.dynamic=XR_FOVEATION_DYNAMIC_DISABLED_FB;
	XrFoveationProfileCreateInfoFB fixedCreate={XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB}; fixedCreate.next=&fixed;
	g.foveationFixedAvailable=create_foveation_profile("xrCreateFoveationProfileFB fixed",fixedCreate,g.foveationFixed);
	if(g.terminal || (!g.foveationFixedAvailable && g.foveationFixed)) return false;
	if(g.foveationEyeSupported && g.xr.FoveationEyeTrackedState) {
		XrFoveationEyeTrackedProfileCreateInfoMETA eye={XR_TYPE_FOVEATION_EYE_TRACKED_PROFILE_CREATE_INFO_META};
		XrFoveationLevelProfileCreateInfoFB eyeLevel={XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB};
		eyeLevel.next=&eye;
		eyeLevel.level=XR_FOVEATION_LEVEL_HIGH_FB;
		eyeLevel.dynamic=XR_FOVEATION_DYNAMIC_LEVEL_ENABLED_FB;
		XrFoveationProfileCreateInfoFB eyeCreate={XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB};
		eyeCreate.next=&eyeLevel;
		g.foveationEyeAvailable=create_foveation_profile("xrCreateFoveationProfileFB dynamic eye",eyeCreate,g.foveationEye);
		if(!g.foveationEyeAvailable && !g.terminal && !g.foveationEye) {
			// A runtime may support eye-tracked profiles without adjusting their
			// strength dynamically. This remains gaze-tracked, never fixed fallback.
			eyeLevel.level=XR_FOVEATION_LEVEL_LOW_FB;
			eyeLevel.dynamic=XR_FOVEATION_DYNAMIC_DISABLED_FB;
			g.foveationEyeAvailable=create_foveation_profile("xrCreateFoveationProfileFB static eye",eyeCreate,g.foveationEye);
		}
		if(g.terminal || (!g.foveationEyeAvailable && g.foveationEye)) return false;
	}
	return update_foveation_profile(g.foveationOff);
}
static void destroy_foveation_profiles() {
	VRF_ResetPolicy(&g.foveationEyePolicy);
	if(g.foveationEye && g.xr.DestroyFoveationProfile) ok("xrDestroyFoveationProfileFB eye",g.xr.DestroyFoveationProfile(g.foveationEye));
	if(g.foveationFixed && g.xr.DestroyFoveationProfile) ok("xrDestroyFoveationProfileFB fixed",g.xr.DestroyFoveationProfile(g.foveationFixed));
	if(g.foveationOff && g.xr.DestroyFoveationProfile) ok("xrDestroyFoveationProfileFB off",g.xr.DestroyFoveationProfile(g.foveationOff));
	g.foveationOff=g.foveationFixed=g.foveationEye=XR_NULL_HANDLE;
	g.foveationFixedAvailable=g.foveationEyeAvailable=false;
}

static void create_xdev_trackers() {
	if (!g.xdevSupported) return;
	XrCreateXDevListInfoMNDX info={XR_TYPE_CREATE_XDEV_LIST_INFO_MNDX};
	if (!ok("xrCreateXDevListMNDX",g.xr.CreateXDevList(g.session,&info,&g.xdevList))) { g.xdevSupported=false; return; }
	uint32_t count=0;
	if (!ok("xrEnumerateXDevsMNDX",g.xr.EnumerateXDevs(g.xdevList,0,&count,0))) return;
	std::vector<XrXDevIdMNDX> ids(count);
	if (!ok("xrEnumerateXDevsMNDX",g.xr.EnumerateXDevs(g.xdevList,count,&count,ids.data()))) return;
	uint32_t skipped=0;
	for (uint32_t i=0;i<count && g.trackers.size() < VRXR_MAX_DEVICES-kTrackerFirst;++i) {
		XrGetXDevInfoMNDX get={XR_TYPE_GET_XDEV_INFO_MNDX}; get.id=ids[i]; XrXDevPropertiesMNDX properties={XR_TYPE_XDEV_PROPERTIES_MNDX};
		if (!ok("xrGetXDevPropertiesMNDX",g.xr.XDevProperties(g.xdevList,&get,&properties)) || !properties.canCreateSpace) continue;
		char name[sizeof(properties.name)]; std::strncpy(name,properties.name,sizeof(name)-1); name[sizeof(name)-1]=0;
		for(char *p=name;*p;++p) *p=(char)std::tolower((unsigned char)*p);
		/* The revision-3 ABI supplies only a display name, serial, and pose
		 * capability.  xrizer uses this same conservative tracker-name gate. */
		if(!std::strstr(name,"tracker")) { ++skipped; continue; }
		XrCreateXDevSpaceInfoMNDX create={XR_TYPE_CREATE_XDEV_SPACE_INFO_MNDX}; create.xdevList=g.xdevList; create.id=ids[i]; create.offset=identity_pose();
		Tracker tracker; std::memset(&tracker,0,sizeof(tracker));
		if (!ok("xrCreateXDevSpaceMNDX",g.xr.CreateXDevSpace(g.session,&create,&tracker.space))) continue;
		if(std::memchr(properties.serial,0,sizeof(properties.serial))) tracker_identity("xrmndx",properties.serial,tracker.serial);
		g.trackers.push_back(tracker);
	}
	if(skipped) say("OpenXR: skipped ambiguous MNDX device spaces (only names containing 'tracker' are exposed)");
}
static void locate_gaze(vrxr_gaze_t *gaze, bool focused) {
	std::memset(gaze,0,sizeof(*gaze));
	if (!g.gazeEnabled || !g.gazeSupported || g.gazeSpace==XR_NULL_HANDLE || !focused) return;
	XrActionStateGetInfo info={XR_TYPE_ACTION_STATE_GET_INFO}; info.action=g.gazeAction;
	XrActionStatePose state={XR_TYPE_ACTION_STATE_POSE};
	if (!ok("xrGetActionStatePose gaze",g.xr.PoseState(g.session,&info,&state)) || !state.isActive) return;
	XrEyeGazeSampleTimeEXT sample={XR_TYPE_EYE_GAZE_SAMPLE_TIME_EXT};
	XrSpaceLocation location={XR_TYPE_SPACE_LOCATION,&sample};
	if (!ok("xrLocateSpace gaze",g.xr.LocateSpace(g.gazeSpace,g.appSpace,g.frameState.predictedDisplayTime,&location))) return;
	gaze->valid=(location.locationFlags&(XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT))==(XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT);
	gaze->tracked=(location.locationFlags&(XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT))==(XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT);
	if (!gaze->valid || !gaze->tracked) return;
	const XrQuaternionf &q=location.pose.orientation;
	const double norm=(double)q.x*q.x+(double)q.y*q.y+(double)q.z*q.z+(double)q.w*q.w;
	if (!std::isfinite(norm) || std::fabs(norm-1.0)>0.01 ||
	    !std::isfinite(location.pose.position.x) || !std::isfinite(location.pose.position.y) ||
	    !std::isfinite(location.pose.position.z)) { gaze->valid=0; return; }
	gaze->origin[0]=location.pose.position.x; gaze->origin[1]=location.pose.position.y; gaze->origin[2]=location.pose.position.z;
	/* Rotate OpenXR's forward vector (0,0,-1); gaze remains in appSpace. */
	gaze->direction[0]=-2.f*(q.x*q.z+q.w*q.y);
	gaze->direction[1]=-2.f*(q.y*q.z-q.w*q.x);
	gaze->direction[2]=-1.f+2.f*(q.x*q.x+q.y*q.y);
	/* Expressed pose time may be predicted/interpolated. It is not necessarily
	 * an acquisition timestamp. Convert before subtraction to avoid signed
	 * overflow on malformed runtime times. */
	if (sample.time) { gaze->sample_time_known=1; gaze->sample_age_seconds=((double)g.frameState.predictedDisplayTime-(double)sample.time)*1e-9; }
}
static bool locate_frame(vrxr_frame_t *frame) {
	XrActiveActionSet active[]={{g.actions,XR_NULL_PATH},{g.gazeActions,XR_NULL_PATH}};
	XrActionsSyncInfo sync={XR_TYPE_ACTIONS_SYNC_INFO}; sync.countActiveActionSets=g.gazeEnabled && g.gazeSupported && g.gazeActions ? 2 : 1; sync.activeActionSets=active;
	XrResult synced=g.xr.SyncActions(g.session,&sync);
	const bool focused=synced==XR_SUCCESS && g.sessionState==XR_SESSION_STATE_FOCUSED;
	if (synced!=XR_SUCCESS && synced!=XR_SESSION_NOT_FOCUSED) { ok("xrSyncActions",synced); return false; }
	frame->focused=focused;
	device_from_space(&frame->devices[0],g.viewSpace,VRXR_DEVICE_HEAD,-1);
	for(int hand=0;hand<kHands;++hand) {
		if (!focused) { frame->devices[hand+1].kind=VRXR_DEVICE_HAND; frame->devices[hand+1].hand=hand; continue; }
		XrActionStateGetInfo info={XR_TYPE_ACTION_STATE_GET_INFO}; info.action=g.action[ACT_GRIP_POSE]; info.subactionPath=g.handPath[hand]; XrActionStatePose state={XR_TYPE_ACTION_STATE_POSE};
		if (ok("xrGetActionStatePose",g.xr.PoseState(g.session,&info,&state)) && state.isActive) device_from_space(&frame->devices[hand+1],g.handSpace[hand],VRXR_DEVICE_HAND,hand);
		else { std::memset(&frame->devices[hand+1],0,sizeof(frame->devices[hand+1])); frame->devices[hand+1].kind=VRXR_DEVICE_HAND; frame->devices[hand+1].hand=hand; }
		input_for_hand(&frame->hands[hand],hand);
		frame->devices[hand+1].connected |= frame->hands[hand].active;
	}
	locate_trackers(frame,focused);
	XrViewState viewState={XR_TYPE_VIEW_STATE}; XrViewLocateInfo locate={XR_TYPE_VIEW_LOCATE_INFO}; locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO; locate.displayTime=g.frameState.predictedDisplayTime; locate.space=g.appSpace;
	uint32_t count=0; for(int i=0;i<kViews;++i) g.views[i].type=XR_TYPE_VIEW;
	if (!ok("xrLocateViews",g.xr.LocateViews(g.session,&locate,&viewState,kViews,&count,g.views)) || count!=kViews) return false;
	const XrViewStateFlags required=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
	if ((viewState.viewStateFlags & required)!=required || !frame->devices[0].valid) {
		g.shouldRender=false; frame->should_render=0;
		return true;
	}
	for(int eye=0;eye<kViews;++eye) { matrix(g.views[eye].pose,frame->views[eye].matrix); frame->views[eye].left=std::tan(g.views[eye].fov.angleLeft); frame->views[eye].right=std::tan(g.views[eye].fov.angleRight); frame->views[eye].up=std::tan(g.views[eye].fov.angleUp); frame->views[eye].down=std::tan(g.views[eye].fov.angleDown); frame->views[eye].width=eye_chain(eye).width; frame->views[eye].height=eye_chain(eye).height; }
	locate_gaze(&frame->gaze,focused);
	return true;
}
static bool wait_chain(Chain &chain) {
	XrSwapchainImageWaitInfo wait={XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wait.timeout=kWaitNs;
	for (int attempt=0;attempt<kImageWaitAttempts;++attempt) {
		XrResult result=g.xr.WaitSwapchainImage(chain.handle,&wait);
		if (result==XR_SUCCESS) { chain.waited=true; return true; }
		if (result==XR_TIMEOUT_EXPIRED) continue; // retry this same acquired image
		if (result==XR_SESSION_LOSS_PENDING) { g.terminal=true; sayf("xrWaitSwapchainImage",result); return false; }
		sayf("xrWaitSwapchainImage",result); g.terminal=true; return false;
	}
	/* A faulty runtime must not trap the engine in an endless retry loop.
	 * Abort destroys the unwaited swapchain; it must never release or draw it.
	 * Nominal budget is 5s; the runtime may exceed an individual wait timeout. */
	say("OpenXR: swapchain wait budget exhausted; tearing down the session");
	return false;
}
static void release_chain(Chain &chain) {
	if (!chain.acquired || !chain.waited) return;
	XrSwapchainImageReleaseInfo release={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
	XrResult result;
	{
		VulkanQueueLock lock;
		result=g.xr.ReleaseSwapchainImage(chain.handle,&release);
	}
	if (!ok("xrReleaseSwapchainImage",result)) g.terminal=true;
	chain.acquired=false; chain.waited=false;
}
static bool begin_images() {
	for(int eye=0;eye<swapchain_count();++eye) {
		Chain &chain=g.chain[eye]; XrSwapchainImageAcquireInfo acquire={XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
		XrResult result;
		{
			VulkanQueueLock lock;
			result=g.xr.AcquireSwapchainImage(chain.handle,&acquire,&chain.index);
		}
		if (!ok("xrAcquireSwapchainImage",result)) { g.terminal=true; return false; }
		chain.acquired=true; chain.copiedMask=0;
		if (!wait_chain(chain)) return false;
	}
	return true;
}
static void end_frame(bool submit) {
	XrCompositionLayerProjectionView projectionViews[kViews]; std::memset(projectionViews,0,sizeof(projectionViews));
	for(int eye=0;eye<kViews;++eye) {
		const Chain &chain=eye_chain(eye);
		projectionViews[eye].type=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
		projectionViews[eye].pose=g.views[eye].pose; projectionViews[eye].fov=g.views[eye].fov;
		projectionViews[eye].subImage.swapchain=chain.handle;
		projectionViews[eye].subImage.imageArrayIndex=swapchain_layers()==kViews ? eye : 0;
		projectionViews[eye].subImage.imageRect.extent.width=(int32_t)chain.width;
		projectionViews[eye].subImage.imageRect.extent.height=(int32_t)chain.height;
	}
	for(int chain=0;chain<swapchain_count();++chain) release_chain(g.chain[chain]);
	XrCompositionLayerProjection layer={XR_TYPE_COMPOSITION_LAYER_PROJECTION}; layer.space=g.appSpace; layer.viewCount=kViews; layer.views=projectionViews;
	const XrCompositionLayerBaseHeader *layers[1]={reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
	XrFrameEndInfo end={XR_TYPE_FRAME_END_INFO}; end.displayTime=g.frameState.predictedDisplayTime; end.environmentBlendMode=g.blend;
	if (submit && !g.terminal) { end.layerCount=1; end.layers=layers; }
	XrResult result;
	{
		VulkanQueueLock lock;
		result=g.xr.EndFrame(g.session,&end);
	}
	if (!ok("xrEndFrame",result)) g.terminal=true;
	g.frameBegun=false; g.shouldRender=false;
}
static void poll_events() {
	for (;;) {
		XrEventDataBuffer event={XR_TYPE_EVENT_DATA_BUFFER}; XrResult result=g.xr.PollEvent(g.instance,&event);
		if (result==XR_EVENT_UNAVAILABLE) break;
		if (!ok("xrPollEvent",result)) { g.terminal=true; break; }
		if (event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) { g.stopReason=VRXR_STOP_INSTANCE_LOST; g.terminal=true; continue; }
		if(g.htcxSupported && (event.type==XR_TYPE_EVENT_DATA_VIVE_TRACKER_CONNECTED_HTCX ||
		    event.type==XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED || event.type==XR_TYPE_EVENT_DATA_EVENTS_LOST))
			g.trackersDirty=true;
		if (event.type==XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR) {
			const XrEventDataVisibilityMaskChangedKHR *change=reinterpret_cast<const XrEventDataVisibilityMaskChangedKHR *>(&event);
			if(change->session==g.session && change->viewConfigurationType==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO && change->viewIndex<kViews)
				g.masks[change->viewIndex].dirty=true;
			continue;
		}
		if (event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
			XrEventDataReferenceSpaceChangePending *change=reinterpret_cast<XrEventDataReferenceSpaceChangePending *>(&event);
			if(g.session && change->session==g.session && change->referenceSpaceType==g.appSpaceType) { g.pendingReferenceType=change->referenceSpaceType; g.pendingReferenceTime=change->changeTime; g.referencePending=true; }
			continue;
		}
		if (event.type!=XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) continue;
		XrEventDataSessionStateChanged *state=reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
		if(!g.session || state->session!=g.session) continue;
		g.sessionState=state->state;
		if (state->state==XR_SESSION_STATE_READY && !g.sessionRunning && !g.terminal) { XrSessionBeginInfo begin={XR_TYPE_SESSION_BEGIN_INFO}; begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO; if(ok("xrBeginSession",g.xr.BeginSession(g.session,&begin))) g.sessionRunning=true; else g.terminal=true; }
		else if (state->state==XR_SESSION_STATE_STOPPING && g.sessionRunning && !g.terminal) { if(!ok("xrEndSession",g.xr.EndSession(g.session))) g.terminal=true; g.sessionRunning=false; }
		else if (state->state==XR_SESSION_STATE_LOSS_PENDING || state->state==XR_SESSION_STATE_EXITING) {
			vrxr_stop_reason_t reason=state->state==XR_SESSION_STATE_LOSS_PENDING ? VRXR_STOP_SESSION_LOST : VRXR_STOP_EXITING;
			if(g.stopReason<reason) g.stopReason=reason;
			g.terminal=true;
		}
	}
}
static bool destroy_session_resources() {
	const bool unwaited=(g.chain[0].acquired && !g.chain[0].waited) ||
	                    (g.chain[1].acquired && !g.chain[1].waited);
	/* The renderer must join CPU callbacks, retire GPU work, and destroy its
	 * views of borrowed images before the runtime frees those images. */
	if(g.vk.retireImages) {
		void (*retire)(void *)=g.vk.retireImages; g.vk.retireImages=0;
		retire(g.vk.owner);
	}
	if (g.frameBegun && !g.terminal && !unwaited) end_frame(false);
	// The renderer's retirement callback has completed all image accesses.
	for(int eye=0;eye<kViews;++eye) {
		if(g.chain[eye].handle && g.xr.DestroySwapchain) g.xr.DestroySwapchain(g.chain[eye].handle);
		g.chain[eye]=Chain();
	}
	/* Profiles refer to this session but no longer to live swapchain images. */
	destroy_foveation_profiles();
	/* Destroying an acquired/unwaited image is legal after graphics completion.
	 * Close the healthy session's begun frame with no destroyed images in it. */
	if (g.frameBegun && !g.terminal) end_frame(false);
	for(size_t i=0;i<g.trackers.size();++i) if(!g.trackers[i].persistent && g.trackers[i].space && g.xr.DestroySpace) g.xr.DestroySpace(g.trackers[i].space);
	for(size_t i=0;i<g.trackerSources.size();++i) if(g.trackerSources[i].space && g.xr.DestroySpace) g.xr.DestroySpace(g.trackerSources[i].space);
	g.trackerSources.clear();
	g.trackers.clear();
	for(int hand=0;hand<kHands;++hand) if(g.handSpace[hand] && g.xr.DestroySpace) g.xr.DestroySpace(g.handSpace[hand]);
	if(g.gazeSpace && g.xr.DestroySpace) g.xr.DestroySpace(g.gazeSpace);
	if(g.viewSpace && g.xr.DestroySpace) g.xr.DestroySpace(g.viewSpace);
	if(g.appSpace && g.xr.DestroySpace) g.xr.DestroySpace(g.appSpace);
	if(g.xdevList && g.xr.DestroyXDevList) g.xr.DestroyXDevList(g.xdevList);
	const bool destroyed=!g.session || !g.xr.DestroySession || ok("xrDestroySession",g.xr.DestroySession(g.session));
	if(!destroyed) {
		g.terminal=true;
		if(g.stopReason==VRXR_STOP_NONE) g.stopReason=VRXR_STOP_FAILURE;
	}
	if(g.gazeActions && g.xr.DestroyActionSet) g.xr.DestroyActionSet(g.gazeActions);
	if(g.actions && g.xr.DestroyActionSet) g.xr.DestroyActionSet(g.actions);
	// Reset only session state. Discovery/dispatch and helper-created Vulkan
	// handles remain owned by the original runtime/renderer setup.
	g.session=XR_NULL_HANDLE;
	g.appSpace=g.viewSpace=g.gazeSpace=XR_NULL_HANDLE;
	g.actions=g.gazeActions=XR_NULL_HANDLE;
	g.gazeAction=g.trackerAction=XR_NULL_HANDLE;
	std::memset(g.handSpace,0,sizeof(g.handSpace));
	std::memset(g.action,0,sizeof(g.action));
	std::memset(g.handPath,0,sizeof(g.handPath));
	std::memset(g.views,0,sizeof(g.views));
	g.frameState=XrFrameState();
	for(int eye=0;eye<kViews;++eye) g.masks[eye]=VisibilityMesh();
	g.xdevList=XR_NULL_HANDLE; g.trackersDirty=false;
	g.initialized=g.sessionRunning=g.frameBegun=g.shouldRender=false;
	g.sessionState=XR_SESSION_STATE_IDLE;
	g.appSpaceType=g.pendingReferenceType=XR_REFERENCE_SPACE_TYPE_LOCAL;
	g.pendingReferenceTime=0; g.referenceChanged=g.referencePending=false;
	g.blend=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	// Optional action/setup failures must not permanently disable capabilities
	// on a later fresh session. User gazeEnabled remains independent.
	g.gazeSupported=g.discoveredGaze;
	g.htcxSupported=g.discoveredHtcx;
	g.xdevSupported=g.discoveredXdev;
	/* Engine input reapplies the archived setting on its next pass. */
	g.trackerEnabled=false;
	g.vk.format=VK_FORMAT_UNDEFINED; g.vk.extraUsage=0;
	g.vk.optionalTransferSourceUnsupported=false;
	g.vk.arrayLayers=1; g.vk.densityMaps=false; g.vk.densityImageFlags=0; g.vk.retireImages=0; g.vk.owner=0;
	return destroyed;
}
static void destroy_resources() {
	destroy_session_resources();
	if(g.terminal && g.stopReason==VRXR_STOP_NONE) g.stopReason=VRXR_STOP_FAILURE;
	if(g.instance && g.xr.DestroyInstance) g.xr.DestroyInstance(g.instance);
	if(g.loader) SDL_UnloadObject(g.loader);
	void (*log)(const char *)=g.log;
	vrxr_stop_reason_t reason=g.stopReason;
	g=State(); g.log=log; g.stopReason=reason;
}

static void destroy_stopped_runtime() {
	if(g.useVulkan && (g.stopReason==VRXR_STOP_EXITING || g.stopReason==VRXR_STOP_SESSION_LOST)) {
		const bool destroyed=destroy_session_resources();
		if(destroyed && (g.stopReason==VRXR_STOP_EXITING || g.stopReason==VRXR_STOP_SESSION_LOST)) {
			g.terminal=false; return;
		}
	}
	destroy_resources();
}

static bool loader_functions() {
	if (!proc(XR_NULL_HANDLE,"xrEnumerateInstanceExtensionProperties",&g.xr.EnumerateExtensions)) return false;
	if (!proc(XR_NULL_HANDLE,"xrCreateInstance",&g.xr.CreateInstance)) return false;
	return true;
}
static bool opaque_blend_mode() {
	PFN_xrEnumerateEnvironmentBlendModes enumerate=0;
	if (!proc(g.instance,"xrEnumerateEnvironmentBlendModes",&enumerate)) return false;
	uint32_t count=0; if(!ok("xrEnumerateEnvironmentBlendModes",enumerate(g.instance,g.system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,0))) return false;
	std::vector<XrEnvironmentBlendMode> modes(count);
	if(!ok("xrEnumerateEnvironmentBlendModes",enumerate(g.instance,g.system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,count,&count,modes.data()))) return false;
	for(size_t i=0;i<modes.size();++i) if(modes[i]==XR_ENVIRONMENT_BLEND_MODE_OPAQUE) { g.blend=modes[i]; return true; }
	say("OpenXR: runtime lacks opaque primary stereo composition"); return false;
}
static LoaderHandle load_loader() {
	const char *loaders[] = {
#if defined(_WIN32)
		"openxr_loader.dll"
#else
		"libopenxr_loader.so.1", "libopenxr_loader.so"
#endif
	};
	// SDL calls dlopen from its own shared library on Linux, so the engine's
	// $ORIGIN RUNPATH does not reliably find a loader beside the executable.
	LoaderHandle library=0;
#ifdef USE_SDL3
	const char *base=SDL_GetBasePath(); // SDL owns this storage in SDL3.
#else
	char *base=SDL_GetBasePath();
#endif
	if(base) {
		for(size_t i=0;i<sizeof(loaders)/sizeof(loaders[0]) && !library;++i)
			library=SDL_LoadObject((std::string(base)+loaders[i]).c_str());
#ifndef USE_SDL3
		SDL_free(base);
#endif
	}
	// Distro/developer installations may provide only a system loader.
	for(size_t i=0;i<sizeof(loaders)/sizeof(loaders[0]) && !library;++i)
		library=SDL_LoadObject(loaders[i]);
	return library;
}
static bool discover_runtime() {
	g.loader=load_loader();
	if (!g.loader) { say("OpenXR: loader unavailable (no runtime is required until OpenXR is selected)"); return false; }
	g.xr.gpa=reinterpret_cast<PFN_xrGetInstanceProcAddr>(SDL_LoadFunction(g.loader,"xrGetInstanceProcAddr"));
	if (!g.xr.gpa || !loader_functions()) return false;
	uint32_t extensionCount=0;
	if(!ok("xrEnumerateInstanceExtensionProperties",g.xr.EnumerateExtensions(0,0,&extensionCount,0))) return false;
	std::vector<XrExtensionProperties> extensions(extensionCount); for(uint32_t i=0;i<extensionCount;++i) extensions[i].type=XR_TYPE_EXTENSION_PROPERTIES;
	if(!ok("xrEnumerateInstanceExtensionProperties",g.xr.EnumerateExtensions(0,extensionCount,&extensionCount,extensions.data()))) return false;
	const char *graphicsExtension=XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME;
	if(!extension(extensions,graphicsExtension)) { say("OpenXR: required graphics extension unavailable"); return false; }
	g.gazeSupported=extension(extensions,XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME);
	g.htcxSupported=extension(extensions,XR_HTCX_VIVE_TRACKER_INTERACTION_EXTENSION_NAME);
	for(size_t i=0;i<extensions.size();++i) if(!std::strcmp(extensions[i].extensionName,XR_HTCX_VIVE_TRACKER_INTERACTION_EXTENSION_NAME)) g.trackerVersion=extensions[i].extensionVersion;
	g.xdevSupported=extension_version(extensions,XR_MNDX_XDEV_SPACE_EXTENSION_NAME,XR_MNDX_xdev_space_SPEC_VERSION);
	g.frameSupported=extension(extensions,"XR_VALVE_frame_controller_interaction");
	g.maskSupported=extension(extensions,XR_KHR_VISIBILITY_MASK_EXTENSION_NAME);
	g.localFloorSupported=extension(extensions,XR_EXT_LOCAL_FLOOR_EXTENSION_NAME);
	if(g.useVulkan) {
		g.vulkanSwapchainImageFlagsSupported=extension(extensions,XR_META_VULKAN_SWAPCHAIN_CREATE_INFO_EXTENSION_NAME);
		g.foveationSupported=extension(extensions,XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME) &&
			extension(extensions,XR_FB_FOVEATION_EXTENSION_NAME) &&
			extension(extensions,XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME) &&
			extension(extensions,XR_FB_FOVEATION_VULKAN_EXTENSION_NAME);
		g.foveationEyeSupported=g.foveationSupported && extension(extensions,XR_META_FOVEATION_EYE_TRACKED_EXTENSION_NAME);
	}
	std::vector<const char *> enabled; enabled.push_back(graphicsExtension);
	if(g.gazeSupported) enabled.push_back(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME);
	if(g.htcxSupported) enabled.push_back(XR_HTCX_VIVE_TRACKER_INTERACTION_EXTENSION_NAME);
	if(g.xdevSupported) enabled.push_back(XR_MNDX_XDEV_SPACE_EXTENSION_NAME);
	if(g.frameSupported) enabled.push_back("XR_VALVE_frame_controller_interaction");
	if(g.maskSupported) enabled.push_back(XR_KHR_VISIBILITY_MASK_EXTENSION_NAME);
	if(g.localFloorSupported) enabled.push_back(XR_EXT_LOCAL_FLOOR_EXTENSION_NAME);
	if(g.foveationSupported) {
		enabled.push_back(XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME);
		enabled.push_back(XR_FB_FOVEATION_EXTENSION_NAME);
		enabled.push_back(XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME);
		enabled.push_back(XR_FB_FOVEATION_VULKAN_EXTENSION_NAME);
	}
	if(g.foveationEyeSupported) enabled.push_back(XR_META_FOVEATION_EYE_TRACKED_EXTENSION_NAME);
	if(g.vulkanSwapchainImageFlagsSupported) enabled.push_back(XR_META_VULKAN_SWAPCHAIN_CREATE_INFO_EXTENSION_NAME);
	XrInstanceCreateInfo create={XR_TYPE_INSTANCE_CREATE_INFO};
	std::strncpy(create.applicationInfo.applicationName,"Quakespasm VR",sizeof(create.applicationInfo.applicationName)-1);
	create.applicationInfo.applicationVersion=1; std::strncpy(create.applicationInfo.engineName,"Quakespasm",sizeof(create.applicationInfo.engineName)-1);
	create.applicationInfo.engineVersion=1; create.applicationInfo.apiVersion=XR_API_VERSION_1_0;
	create.enabledExtensionCount=(uint32_t)enabled.size(); create.enabledExtensionNames=enabled.data();
	if(!ok("xrCreateInstance",g.xr.CreateInstance(&create,&g.instance))) return false;
	if(!load_instance_functions()) return false;
	XrSystemGetInfo system={XR_TYPE_SYSTEM_GET_INFO}; system.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	if(!ok("xrGetSystem",g.xr.GetSystem(g.instance,&system,&g.system))) return false;
	XrSystemEyeGazeInteractionPropertiesEXT gaze={XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT};
	XrSystemXDevSpacePropertiesMNDX xdev={XR_TYPE_SYSTEM_XDEV_SPACE_PROPERTIES_MNDX};
	XrSystemFoveationEyeTrackedPropertiesMETA foveationEye={XR_TYPE_SYSTEM_FOVEATION_EYE_TRACKED_PROPERTIES_META};
	XrSystemProperties properties={XR_TYPE_SYSTEM_PROPERTIES};
	void **next=&properties.next;
	if(g.gazeSupported) { *next=&gaze; next=&gaze.next; }
	if(g.xdevSupported) { *next=&xdev; next=&xdev.next; }
	if(g.foveationEyeSupported) *next=&foveationEye;
	if(!ok("xrGetSystemProperties",g.xr.GetSystemProperties(g.instance,g.system,&properties))) return false;
	std::memcpy(g.systemName,properties.systemName,sizeof(g.systemName));
	g.systemName[sizeof(g.systemName)-1]=0;
	g.gazeSupported=g.gazeSupported && gaze.supportsEyeGazeInteraction==XR_TRUE;
	g.xdevSupported=g.xdevSupported && xdev.supportsXDevSpace==XR_TRUE;
	g.foveationEyeSupported=g.foveationEyeSupported && foveationEye.supportsFoveationEyeTracked==XR_TRUE;
	{
		char line[192];
		std::snprintf(line,sizeof(line),
			"OpenXR foveation extensions: FB set %s, META eye profile %s, EXT gaze action %s",
			g.foveationSupported ? "yes" : "no", g.foveationEyeSupported ? "yes" : "no",
			g.gazeSupported ? "yes" : "no");
		say(line);
	}
	XrInstanceProperties instanceProperties={XR_TYPE_INSTANCE_PROPERTIES};
	if(ok("xrGetInstanceProperties",g.xr.GetInstanceProperties(g.instance,&instanceProperties))) std::memcpy(g.runtime,instanceProperties.runtimeName,sizeof(g.runtime));
	g.runtime[sizeof(g.runtime)-1]=0;
	g.discoveredGaze=g.gazeSupported;
	g.discoveredHtcx=g.htcxSupported;
	g.discoveredXdev=g.xdevSupported;
	return true;
}
static bool finish_session() {
	if(!opaque_blend_mode()) return false;
	if(create_space(XR_REFERENCE_SPACE_TYPE_STAGE,&g.appSpace)) g.appSpaceType=XR_REFERENCE_SPACE_TYPE_STAGE;
	else if(g.localFloorSupported && create_space(XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT,&g.appSpace)) { g.appSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT; say("OpenXR: STAGE unavailable; using runtime LOCAL_FLOOR"); }
	else if(create_space(XR_REFERENCE_SPACE_TYPE_LOCAL,&g.appSpace)) { g.appSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL; say("OpenXR: STAGE/LOCAL_FLOOR unavailable; LOCAL calibration fallback is required"); }
	else return false;
	if(!create_space(XR_REFERENCE_SPACE_TYPE_VIEW,&g.viewSpace)) return false;
	if(!create_actions()) return false;
	if(!create_swapchains()) return false;
	if(!create_foveation_profiles()) return false;
	if(g.htcxSupported) say("OpenXR: HTCX tracker poses use persistent identities; new trackers need a unique runtime role or VR restart");
	else create_xdev_trackers();
	if(!g.htcxSupported && g.xdevSupported) say("OpenXR: MNDX xdev spaces are exposed as generic tracker slots; this preview ABI has no tracker class field");
	if(g.terminal) return false;
	g.initialized=true;
	return true;
}
} /* namespace */

extern "C" void VRXR_Shutdown(void) { destroy_resources(); }
extern "C" vrxr_stop_reason_t VRXR_StopReason(void) { return g.stopReason; }
extern "C" void VRXR_SetTrackerEnabled(int enabled) { g.trackerEnabled=enabled!=0; }

extern "C" int VRXR_BeginFrame(vrxr_frame_t *frame) {
	if(!frame) return -1;
	std::memset(frame,0,sizeof(*frame));
	if(!g.initialized) return -1;
	if(g.terminal) { destroy_stopped_runtime(); return -1; }
	if(g.frameBegun) { say("OpenXR: nested BeginFrame rejected"); return -1; }
	poll_events();
	if(g.terminal) { destroy_stopped_runtime(); return -1; }
	if(!g.sessionRunning) return 0;
	XrFrameWaitInfo wait={XR_TYPE_FRAME_WAIT_INFO}; g.frameState.type=XR_TYPE_FRAME_STATE;
	XrResult result=g.xr.WaitFrame(g.session,&wait,&g.frameState);
	if(result==XR_SESSION_LOSS_PENDING) { g.terminal=true; sayf("xrWaitFrame",result); destroy_stopped_runtime(); return -1; }
	if(!ok("xrWaitFrame",result)) { g.terminal=true; destroy_stopped_runtime(); return -1; }
	XrFrameBeginInfo begin={XR_TYPE_FRAME_BEGIN_INFO};
	{
		VulkanQueueLock lock;
		result=g.xr.BeginFrame(g.session,&begin);
	}
	if(result!=XR_SUCCESS && result!=XR_FRAME_DISCARDED) {
		ok("xrBeginFrame",result); g.terminal=true; destroy_stopped_runtime(); return -1;
	}
	g.frameBegun=true; g.shouldRender=g.frameState.shouldRender==XR_TRUE;
	if(g.referencePending && g.pendingReferenceType==g.appSpaceType && g.pendingReferenceTime<=g.frameState.predictedDisplayTime) { g.referenceChanged=true; g.referencePending=false; }
	frame->should_render=g.shouldRender; frame->focused=g.sessionState==XR_SESSION_STATE_FOCUSED; frame->reference_changed=g.referenceChanged ? 1 : 0;
	frame->floor_referenced=(g.appSpaceType==XR_REFERENCE_SPACE_TYPE_STAGE || g.appSpaceType==XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT) ? 1 : 0;
	g.referenceChanged=false;
	if(!locate_frame(frame)) { VRXR_AbortFrame(); return -1; }
	if(g.shouldRender && !begin_images()) { VRXR_AbortFrame(); return -1; }
	if(++g_sample_id == 0) ++g_sample_id;
	frame->sample_id=g_sample_id;
	return 1;
}

extern "C" void VRXR_EndFrame(void) {
	if(!g.frameBegun) return;
	bool submit=g.shouldRender && (eye_chain(0).copiedMask&1u) && (eye_chain(1).copiedMask&2u);
	if(!submit) VRF_ResetPolicy(&g.foveationEyePolicy);
	if(g.shouldRender && !submit) say("OpenXR: incomplete stereo submission; submitting an empty frame");
	end_frame(submit);
	if(g.terminal) destroy_stopped_runtime();
}

extern "C" void VRXR_AbortFrame(void) {
	if(g.terminal) { destroy_stopped_runtime(); return; }
	if(!g.frameBegun) return;
	VRF_ResetPolicy(&g.foveationEyePolicy);
	for(int eye=0;eye<kViews;++eye) if(g.chain[eye].acquired && !g.chain[eye].waited) {
		g.stopReason=VRXR_STOP_FAILURE;
		destroy_stopped_runtime(); return; // Host_Error cleanup must not wait again
	}
	end_frame(false);
}

extern "C" void VRXR_Haptic(int physical_hand, float duration_seconds, float amplitude) {
	if(!g.initialized || g.terminal || !g.sessionRunning || g.sessionState!=XR_SESSION_STATE_FOCUSED || physical_hand<0 || physical_hand>=kHands || !std::isfinite(duration_seconds) || !std::isfinite(amplitude) || duration_seconds<=0.f) return;
	duration_seconds=std::min(duration_seconds,5.0f);
	XrHapticActionInfo action={XR_TYPE_HAPTIC_ACTION_INFO}; action.action=g.action[ACT_HAPTIC]; action.subactionPath=g.handPath[physical_hand];
	XrHapticVibration vibration={XR_TYPE_HAPTIC_VIBRATION}; vibration.duration=(XrDuration)(duration_seconds*1000000000.0f); vibration.frequency=XR_FREQUENCY_UNSPECIFIED; vibration.amplitude=std::max(0.f,std::min(1.f,amplitude));
	ok("xrApplyHapticFeedback",g.xr.Haptic(g.session,&action,reinterpret_cast<const XrHapticBaseHeader *>(&vibration)));
}

extern "C" int VRXR_GetViewSize(int eye, unsigned int *width, unsigned int *height) {
	if(!g.initialized || eye<0 || eye>=kViews || !width || !height) return 0;
	*width=eye_chain(eye).width; *height=eye_chain(eye).height; return 1;
}
extern "C" const char *VRXR_RuntimeName(void) { return g.initialized && g.runtime[0] ? g.runtime : ""; }
extern "C" const char *VRXR_SystemName(void) { return g.initialized && g.systemName[0] ? g.systemName : ""; }
extern "C" void VRXR_SetGazeEnabled(int enabled) { g.gazeEnabled=enabled!=0; }
extern "C" int VRXR_GazeSupported(void) { return g.initialized && g.gazeSupported ? 1 : 0; }

extern "C" uint32_t VRXR_GetHiddenAreaMesh(int eye, const float **vertices) {
	if(!vertices) return 0;
	*vertices=0;
	if(!g.initialized || !g.maskSupported || eye<0 || eye>=kViews) return 0;
	VisibilityMesh &cached=g.masks[eye];
	if(cached.dirty) {
		cached.dirty=false;
		cached.triangles.clear(); // A failed refresh must not leave an obsolete mask.
		XrVisibilityMaskKHR mask={XR_TYPE_VISIBILITY_MASK_KHR};
		XrResult result=g.xr.VisibilityMask(g.session,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,eye,
			XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR,&mask);
		if(result!=XR_SUCCESS || !mask.vertexCountOutput || !mask.indexCountOutput) return 0;
		const uint32_t limit=3u*(1u<<20);
		if(mask.vertexCountOutput>limit || mask.indexCountOutput>limit || mask.indexCountOutput%3) return 0;
		std::vector<XrVector2f> points(mask.vertexCountOutput);
		std::vector<uint32_t> indices(mask.indexCountOutput);
		mask.vertexCapacityInput=(uint32_t)points.size(); mask.vertices=points.data();
		mask.indexCapacityInput=(uint32_t)indices.size(); mask.indices=indices.data();
		result=g.xr.VisibilityMask(g.session,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,eye,
			XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR,&mask);
		if(result==XR_ERROR_SIZE_INSUFFICIENT) { cached.dirty=true; return 0; }
		if(result!=XR_SUCCESS || mask.vertexCountOutput>points.size() || mask.indexCountOutput>indices.size() || mask.indexCountOutput%3) return 0;
		for(uint32_t i=0;i<mask.indexCountOutput;++i) {
			if(indices[i]>=mask.vertexCountOutput) return 0;
			const XrVector2f &p=points[indices[i]];
			if(!std::isfinite(p.x) || !std::isfinite(p.y)) return 0;
		}
		cached.triangles.reserve((size_t)mask.indexCountOutput*2);
		for(uint32_t i=0;i<mask.indexCountOutput;++i) {
			cached.triangles.push_back(points[indices[i]].x);
			cached.triangles.push_back(points[indices[i]].y);
		}
	}
	*vertices=cached.triangles.empty() ? 0 : cached.triangles.data();
	return (uint32_t)(cached.triangles.size()/6);
}

namespace {
static bool vulkan_result(const char *where, XrResult xrResult, VkResult vkResult) {
	if(!ok(where,xrResult)) return false;
	if(vkResult==VK_SUCCESS) return true;
	char message[160]; std::snprintf(message,sizeof(message),"OpenXR: %s Vulkan result %d",where,(int)vkResult);
	say(message); return false;
}
static uint32_t vulkan_version(XrVersion version) {
	/* Vulkan has fewer major-version bits than XrVersion. Saturate the upper
	 * bound rather than overflowing an unconstrained runtime maximum. OpenXR
	 * graphics requirements describe major/minor versions, ignoring patches. */
	if(XR_VERSION_MAJOR(version)>0x7f || XR_VERSION_MINOR(version)>0x3ff)
		return VK_MAKE_API_VERSION(0,0x7f,0x3ff,0);
	return VK_MAKE_API_VERSION(0,XR_VERSION_MAJOR(version),XR_VERSION_MINOR(version),0);
}
}
extern "C" int VRXR_PrepareVulkan(void (*log_message)(const char *),
                                  uint32_t *minimum_version, uint32_t *maximum_version) {
	if(!minimum_version || !maximum_version) return 0;
	*minimum_version=0; *maximum_version=0;
	if(g.instance || g.initialized) { say("OpenXR: shutdown the previous binding before Vulkan discovery"); return 0; }
	g=State(); g.log=log_message; g.useVulkan=true;
	if(!discover_runtime()) { destroy_resources(); return 0; }
	g.vk.requirements.type=XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR;
	if(!ok("xrGetVulkanGraphicsRequirements2KHR",g.xr.VulkanRequirements(g.instance,g.system,&g.vk.requirements))) {
		destroy_resources(); return 0;
	}
	*minimum_version=vulkan_version(g.vk.requirements.minApiVersionSupported);
	*maximum_version=vulkan_version(g.vk.requirements.maxApiVersionSupported);
	return 1;
}
extern "C" int VRXR_CreateVulkanInstance(PFN_vkGetInstanceProcAddr get_proc,
                                         const VkInstanceCreateInfo *info, VkInstance *instance) {
	if(!instance) return 0;
	*instance=VK_NULL_HANDLE;
	if(!g.useVulkan || !g.instance || g.terminal || g.vk.instance || !get_proc || !info) return 0;
	const uint32_t version=info->pApplicationInfo && info->pApplicationInfo->apiVersion ? info->pApplicationInfo->apiVersion : VK_API_VERSION_1_0;
	const XrVersion xrVersion=XR_MAKE_VERSION(VK_API_VERSION_MAJOR(version),VK_API_VERSION_MINOR(version),0);
	const XrVersion minimum=XR_MAKE_VERSION(XR_VERSION_MAJOR(g.vk.requirements.minApiVersionSupported),
		XR_VERSION_MINOR(g.vk.requirements.minApiVersionSupported),0);
	const XrVersion maximum=XR_MAKE_VERSION(XR_VERSION_MAJOR(g.vk.requirements.maxApiVersionSupported),
		XR_VERSION_MINOR(g.vk.requirements.maxApiVersionSupported),0);
	if(VK_API_VERSION_VARIANT(version) || xrVersion<minimum) {
		say("OpenXR: requested Vulkan API version does not meet runtime requirements"); return 0;
	}
	/* The maximum is the runtime's highest tested version, not a prohibition
	 * on compatible newer Vulkan versions (XR_KHR_vulkan_enable2). */
	if(xrVersion>maximum) say("OpenXR: requested Vulkan API is newer than the runtime's tested version");
	XrVulkanInstanceCreateInfoKHR create={XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
	create.systemId=g.system; create.pfnGetInstanceProcAddr=get_proc; create.vulkanCreateInfo=info;
	VkResult result=VK_ERROR_INITIALIZATION_FAILED;
	XrResult xrResult=g.xr.CreateVulkanInstance(g.instance,&create,instance,&result);
	if(!vulkan_result("xrCreateVulkanInstanceKHR",xrResult,result) || !*instance) return 0;
	g.vk.getProc=get_proc; g.vk.instance=*instance; g.vk.apiVersion=version; return 1;
}
extern "C" VkPhysicalDevice VRXR_VulkanPhysicalDevice(VkInstance instance) {
	if(!g.useVulkan || !g.instance || g.terminal || !instance || instance!=g.vk.instance) return VK_NULL_HANDLE;
	if(g.vk.physicalDevice) return g.vk.physicalDevice;
	XrVulkanGraphicsDeviceGetInfoKHR info={XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
	info.systemId=g.system; info.vulkanInstance=instance;
	VkPhysicalDevice physical=VK_NULL_HANDLE;
	if(!ok("xrGetVulkanGraphicsDevice2KHR",g.xr.VulkanGraphicsDevice(g.instance,&info,&physical))) return VK_NULL_HANDLE;
	g.vk.physicalDevice=physical; return physical;
}
extern "C" int VRXR_CreateVulkanDevice(const VkDeviceCreateInfo *info, VkDevice *device) {
	if(!device) return 0;
	*device=VK_NULL_HANDLE;
	if(!g.useVulkan || !g.instance || g.terminal || !g.vk.physicalDevice || g.vk.device || !info ||
	   !info->queueCreateInfoCount || !info->pQueueCreateInfos) return 0;
	XrVulkanDeviceCreateInfoKHR create={XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
	create.systemId=g.system; create.pfnGetInstanceProcAddr=g.vk.getProc;
	create.vulkanPhysicalDevice=g.vk.physicalDevice; create.vulkanCreateInfo=info;
	VkResult result=VK_ERROR_INITIALIZATION_FAILED;
	XrResult xrResult=g.xr.CreateVulkanDevice(g.instance,&create,device,&result);
	if(!vulkan_result("xrCreateVulkanDeviceKHR",xrResult,result) || !*device) return 0;
	g.vk.device=*device;
	bool densityExtensionEnabled=false, densityFeatureEnabled=false;
	for(uint32_t i=0;i<info->enabledExtensionCount;++i)
		if(info->ppEnabledExtensionNames && info->ppEnabledExtensionNames[i] &&
		   !std::strcmp(info->ppEnabledExtensionNames[i],VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME))
			densityExtensionEnabled=true;
	for(const VkBaseInStructure *next=reinterpret_cast<const VkBaseInStructure*>(info->pNext); next;
	    next=reinterpret_cast<const VkBaseInStructure*>(next->pNext))
		if(next->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT)
			densityFeatureEnabled=reinterpret_cast<const VkPhysicalDeviceFragmentDensityMapFeaturesEXT*>(next)->fragmentDensityMap==VK_TRUE;
	g.vk.fragmentDensityMapEnabled=densityExtensionEnabled && densityFeatureEnabled;
	for(uint32_t i=0;i<info->queueCreateInfoCount;++i) {
		const VkDeviceQueueCreateInfo &queue=info->pQueueCreateInfos[i];
		VulkanQueue saved={queue.queueFamilyIndex,queue.queueCount,queue.flags}; g.vk.queues.push_back(saved);
	}
	return 1;
}
extern "C" int VRXR_SetVulkanQueueCallbacks(void (*lock)(void *), void (*unlock)(void *), void *owner) {
	if(g.session || (!!lock != !!unlock)) return 0;
	g.vk.lockQueue=lock;
	g.vk.unlockQueue=unlock;
	g.vk.queueOwner=lock ? owner : 0;
	return 1;
}
extern "C" void VRXR_DetachVulkan(void) {
	if(!g.useVulkan) return;
	if(g.terminal) { destroy_stopped_runtime(); return; }
	if(!destroy_session_resources()) { destroy_resources(); return; }
	if(g.terminal) destroy_stopped_runtime();
}
extern "C" int VRXR_VulkanRetryAvailable(void) {
	return g.useVulkan && g.instance && !g.session && !g.terminal &&
		g.vk.instance && g.vk.physicalDevice && g.vk.device && g.vk.apiVersion &&
		(g.stopReason==VRXR_STOP_NONE || g.stopReason==VRXR_STOP_EXITING ||
		 g.stopReason==VRXR_STOP_SESSION_LOST) ? 1 : 0;
}
static bool qualify_vulkan_binding() {
	XrSystemGetInfo systemInfo={XR_TYPE_SYSTEM_GET_INFO};
	systemInfo.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	XrSystemId system=XR_NULL_SYSTEM_ID;
	if(!ok("xrGetSystem on attachment",g.xr.GetSystem(g.instance,&systemInfo,&system))) return false;
	if(system!=g.system) { say("OpenXR: system changed; restart to select a new Vulkan binding"); return false; }
	XrGraphicsRequirementsVulkan2KHR requirements={XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
	if(!ok("xrGetVulkanGraphicsRequirements2KHR on attachment",g.xr.VulkanRequirements(g.instance,system,&requirements))) return false;
	const XrVersion selected=XR_MAKE_VERSION(VK_API_VERSION_MAJOR(g.vk.apiVersion),VK_API_VERSION_MINOR(g.vk.apiVersion),0);
	const XrVersion minimum=XR_MAKE_VERSION(XR_VERSION_MAJOR(requirements.minApiVersionSupported),XR_VERSION_MINOR(requirements.minApiVersionSupported),0);
	if(!g.vk.apiVersion || selected<minimum) { say("OpenXR: Vulkan API requirements changed; restart to select a new binding"); return false; }
	XrVulkanGraphicsDeviceGetInfoKHR info={XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
	info.systemId=system; info.vulkanInstance=g.vk.instance;
	VkPhysicalDevice physical=VK_NULL_HANDLE;
	if(!ok("xrGetVulkanGraphicsDevice2KHR on attachment",g.xr.VulkanGraphicsDevice(g.instance,&info,&physical))) return false;
	if(!physical || physical!=g.vk.physicalDevice) { say("OpenXR: selected GPU changed; restart to select a new Vulkan binding"); return false; }
	return true;
}
extern "C" int VRXR_AttachVulkan(uint32_t queue_family, uint32_t queue_index,
                                 VkImageUsageFlags extra_image_usage, uint32_t array_layers,
                                 void (*retire_images)(void *), void *owner, int density_maps,
                                 VkImageCreateFlags density_image_flags) {
	if(!g.useVulkan || !g.instance || g.terminal || !g.vk.device || g.session || !retire_images ||
	   !g.vk.lockQueue || !g.vk.unlockQueue || (array_layers!=1 && array_layers!=kViews)) return 0;
	if(density_maps && !g.foveationSupported) {
		say("OpenXR: Vulkan fragment density maps requested but foveation is unavailable"); return 0;
	}
	if(density_maps && !g.vk.fragmentDensityMapEnabled) {
		say("OpenXR: Vulkan fragment density maps require VK_EXT_fragment_density_map and its device feature"); return 0;
	}
	const VkImageCreateFlags allowed_density_image_flags=VK_IMAGE_CREATE_SUBSAMPLED_BIT_EXT |
		VK_IMAGE_CREATE_FRAGMENT_DENSITY_MAP_OFFSET_BIT_QCOM;
	if((density_image_flags & ~allowed_density_image_flags) ||
	   (density_image_flags && (!density_maps || !g.vulkanSwapchainImageFlagsSupported))) return 0;
	// The runtime may have gone away while VR was disabled. Do not create a
	// session until pending instance events have been processed.
	// Polling failures describe this attempt, not the previous recoverable stop.
	// Restore the old reason only after a healthy poll, for qualification refusals.
	const vrxr_stop_reason_t previousStop=g.stopReason;
	g.stopReason=VRXR_STOP_NONE;
	poll_events();
	if(g.terminal) { destroy_stopped_runtime(); return 0; }
	g.stopReason=previousStop;
	bool created=false;
	for(size_t i=0;i<g.vk.queues.size();++i)
		if(g.vk.queues[i].family==queue_family && queue_index<g.vk.queues[i].count && !g.vk.queues[i].flags) created=true;
	if(!created) { say("OpenXR: Vulkan binding queue was not created as an ordinary graphics queue"); return 0; }
	PFN_vkGetPhysicalDeviceQueueFamilyProperties properties=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
		g.vk.getProc(g.vk.instance,"vkGetPhysicalDeviceQueueFamilyProperties"));
	if(!properties) return 0;
	uint32_t count=0; properties(g.vk.physicalDevice,&count,0);
	if(queue_family>=count) return 0;
	std::vector<VkQueueFamilyProperties> families(count); properties(g.vk.physicalDevice,&count,families.data());
	if(queue_family>=count || !(families[queue_family].queueFlags&VK_QUEUE_GRAPHICS_BIT)) return 0;
	const VkImageUsageFlags allowed=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
	if(extra_image_usage&~allowed) return 0;
	if(!qualify_vulkan_binding()) {
		if(g.terminal) destroy_stopped_runtime();
		return 0;
	}
	g.vk.extraUsage=0;
	g.vk.optionalTransferSourceUnsupported=false;
	if(extra_image_usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT) g.vk.extraUsage|=XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
	if(extra_image_usage&VK_IMAGE_USAGE_TRANSFER_DST_BIT) g.vk.extraUsage|=XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
	if(extra_image_usage&VK_IMAGE_USAGE_SAMPLED_BIT) g.vk.extraUsage|=XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
	g.vk.retireImages=retire_images; g.vk.owner=owner; g.vk.arrayLayers=array_layers; g.vk.densityMaps=density_maps!=0;
	g.vk.densityImageFlags=density_image_flags;
	XrGraphicsBindingVulkan2KHR binding={XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
	binding.instance=g.vk.instance; binding.physicalDevice=g.vk.physicalDevice; binding.device=g.vk.device;
	binding.queueFamilyIndex=queue_family; binding.queueIndex=queue_index;
	XrSessionCreateInfo create={XR_TYPE_SESSION_CREATE_INFO}; create.systemId=g.system; create.next=&binding;
	g.stopReason=VRXR_STOP_NONE; // a qualified session attempt starts a new outcome
	if(!ok("xrCreateSession Vulkan",g.xr.CreateSession(g.instance,&create,&g.session)) || !finish_session() || g.terminal) {
		/* A mirror's transfer source is optional. Keep the same density-map
		 * profile on this retry; only then consider the existing density fallback. */
		const bool retryWithoutTransferSource=!!(extra_image_usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT) &&
			g.vk.optionalTransferSourceUnsupported && !g.terminal;
		const bool retryWithoutDensityMaps=density_maps && !g.terminal;
		VRXR_DetachVulkan();
		if(retryWithoutTransferSource) {
			say("OpenXR: runtime rejected optional mirror transfer source; retrying without desktop mirror");
			return VRXR_AttachVulkan(queue_family,queue_index,
				extra_image_usage&~VK_IMAGE_USAGE_TRANSFER_SRC_BIT,array_layers,
				retire_images,owner,density_maps,density_image_flags);
		}
		if(retryWithoutDensityMaps) {
			say("OpenXR: runtime density-map setup failed; retrying ordinary VR swapchains");
			return VRXR_AttachVulkan(queue_family,queue_index,extra_image_usage,array_layers,
				retire_images,owner,0,0);
		}
		return 0;
	}
	g.stopReason=VRXR_STOP_NONE;
	say("OpenXR: initialized native Vulkan binding"); return 1;
}
extern "C" int VRXR_VulkanFoveationSupported(void) { return g.useVulkan && g.foveationSupported ? 1 : 0; }
extern "C" int VRXR_VulkanFoveationEyeSupported(void) { return g.useVulkan && g.foveationEyeSupported ? 1 : 0; }
extern "C" int VRXR_VulkanFoveationFixedAvailable(void) { return g.useVulkan && g.session && g.vk.densityMaps && g.foveationFixedAvailable ? 1 : 0; }
extern "C" int VRXR_VulkanFoveationEyeAvailable(void) { return g.useVulkan && g.session && g.vk.densityMaps && g.foveationEyeAvailable ? 1 : 0; }
extern "C" int VRXR_VulkanSwapchainImageFlagsSupported(void) { return g.useVulkan && g.vulkanSwapchainImageFlagsSupported ? 1 : 0; }
extern "C" int VRXR_VulkanTransferSourceAvailable(void) {
	return g.useVulkan && g.initialized && g.session && !g.terminal &&
		(g.vk.extraUsage&XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT) ? 1 : 0;
}
extern "C" VkFormat VRXR_VulkanColorFormat(void) {
	return g.useVulkan && g.initialized && g.session && !g.terminal ? g.vk.format : VK_FORMAT_UNDEFINED;
}
static bool foveation_frame_ready() {
	if(!g.useVulkan || !g.initialized || !g.session || !g.sessionRunning || !g.frameBegun || !g.shouldRender || g.terminal) return false;
	for(int eye=0;eye<swapchain_count();++eye)
		if(!g.chain[eye].acquired || !g.chain[eye].waited || g.chain[eye].copiedMask) return false;
	return true;
}
static bool restore_foveation_off() {
	return g.foveationOff && update_foveation_profile(g.foveationOff);
}
extern "C" int VRXR_UpdateVulkanFoveation(int mode, int allow_eye_tracking, float centers[2][2]) {
	if(!centers) return -1;
	std::memset(centers,0,sizeof(float)*4);
	if(!foveation_frame_ready()) { VRF_ResetPolicy(&g.foveationEyePolicy); return -1; }
	if(!g.vk.densityMaps) { VRF_ResetPolicy(&g.foveationEyePolicy); return 0; }
	if(!g.foveationOff || !g.xr.UpdateSwapchain) { VRF_ResetPolicy(&g.foveationEyePolicy); return -1; }
	XrFoveationProfileFB profile=g.foveationOff;
	int effective=0;
	if(mode==1 && g.sessionState==XR_SESSION_STATE_FOCUSED && g.foveationFixedAvailable) { profile=g.foveationFixed; effective=1; }
	else if(mode==2 && g.sessionState==XR_SESSION_STATE_FOCUSED && allow_eye_tracking && g.foveationEyeAvailable) {
		profile=g.foveationEye; effective=2;
	}
	if(effective!=2) VRF_ResetPolicy(&g.foveationEyePolicy);
	if(!update_foveation_profile(profile)) {
		VRF_ResetPolicy(&g.foveationEyePolicy);
		if(g.terminal || !restore_foveation_off()) return -1;
		return 0;
	}
	if(effective!=2) return effective;
	XrFoveationEyeTrackedStateMETA state={XR_TYPE_FOVEATION_EYE_TRACKED_STATE_META};
	if(!g.xr.FoveationEyeTrackedState || !ok("xrGetFoveationEyeTrackedStateMETA",g.xr.FoveationEyeTrackedState(g.session,&state)) ||
	   !(state.flags&XR_FOVEATION_EYE_TRACKED_STATE_VALID_BIT_META)) {
		VRF_ResetPolicy(&g.foveationEyePolicy);
		if(g.terminal || !restore_foveation_off()) return -1;
		return 0;
	}
	for(uint32_t eye=0;eye<XR_FOVEATION_CENTER_SIZE_META;++eye) {
		const XrVector2f &center=state.foveationCenter[eye];
		if(!std::isfinite(center.x) || !std::isfinite(center.y) || center.x < -1.f || center.x > 1.f || center.y < -1.f || center.y > 1.f) {
			VRF_ResetPolicy(&g.foveationEyePolicy);
			if(!restore_foveation_off()) return -1;
			return 0;
		}
	}
	if(!VRF_AdvanceEyeStability(&g.foveationEyePolicy,1)) {
		if(!restore_foveation_off()) { VRF_ResetPolicy(&g.foveationEyePolicy); return -1; }
		return 0;
	}
	// Publish both eyes together; an invalid second eye must not leak the
	// first eye's center through an off/error result.
	for(uint32_t eye=0;eye<XR_FOVEATION_CENTER_SIZE_META;++eye) {
		centers[eye][0]=state.foveationCenter[eye].x;
		centers[eye][1]=state.foveationCenter[eye].y;
	}
	return 2;
}
static int vulkan_image(int eye, uint32_t index, vrxr_vulkan_eye_t *target) {
	const Chain &chain=eye_chain(eye);
	if(index>=chain.vulkanImages.size()) return 0;
	target->image=chain.vulkanImages[index].image; target->format=g.vk.format;
	target->index=index; target->width=chain.width; target->height=chain.height;
	target->array_layers=swapchain_layers(); target->array_layer=target->array_layers==kViews ? eye : 0;
	target->requested_color_image_flags=g.vk.densityImageFlags;
	if(g.vk.densityMaps && index<chain.densityImages.size()) {
		target->density_image=chain.densityImages[index].image;
		target->density_width=chain.densityImages[index].width;
		target->density_height=chain.densityImages[index].height;
	}
	return target->image!=VK_NULL_HANDLE;
}
extern "C" uint32_t VRXR_VulkanImageCount(int eye) {
	if(!g.useVulkan || !g.initialized || !g.session || g.terminal || eye<0 || eye>=kViews) return 0;
	return (uint32_t)eye_chain(eye).vulkanImages.size();
}
extern "C" int VRXR_GetVulkanImage(int eye, uint32_t index, vrxr_vulkan_eye_t *target) {
	if(!target) return 0;
	std::memset(target,0,sizeof(*target));
	if(index>=VRXR_VulkanImageCount(eye)) return 0;
	return vulkan_image(eye,index,target);
}
extern "C" int VRXR_GetVulkanEye(int eye, vrxr_vulkan_eye_t *target) {
	if(!target) return 0;
	std::memset(target,0,sizeof(*target));
	if(!g.useVulkan || !g.frameBegun || !g.shouldRender || g.terminal || eye<0 || eye>=kViews) return 0;
	const Chain &chain=eye_chain(eye);
	if(!chain.acquired || !chain.waited || (chain.copiedMask&(1u<<eye))) return 0;
	return vulkan_image(eye,chain.index,target);
}
extern "C" int VRXR_VulkanEyeSubmitted(int eye) {
	vrxr_vulkan_eye_t target;
	if(!VRXR_GetVulkanEye(eye,&target)) return 0;
	eye_chain(eye).copiedMask|=1u<<eye; return 1;
}

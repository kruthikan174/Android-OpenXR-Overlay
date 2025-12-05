#include <android/log.h>
#include "android_native_app_glue.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <memory>
#include <vector>
#include <cstring>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#define TAG "OverlayAppGreen"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#define XR_EXTX_OVERLAY_EXTENSION_NAME "XR_EXTX_overlay"
#define XR_TYPE_SESSION_CREATE_INFO_OVERLAY_EXTX ((XrStructureType) 1000033000)

typedef enum XrSessionLayersPlacementEXTX {
    XR_SESSION_LAYERS_PLACEMENT_OVERLAY_EXTX = 1,
    XR_SESSION_LAYERS_PLACEMENT_MAX_ENUM_EXTX = 0x7FFFFFFF
} XrSessionLayersPlacementEXTX;

typedef struct XrSessionCreateInfoOverlayEXTX {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrSessionCreateFlags createFlags;
    XrSessionLayersPlacementEXTX sessionLayersPlacement;
} XrSessionCreateInfoOverlayEXTX;


struct AppState {
    struct android_app* app;
    bool resumed = false;
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
    XrSpace appSpace = XR_NULL_HANDLE;
    bool sessionRunning = false;
    XrViewConfigurationType viewConfigType;
    XrEnvironmentBlendMode blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

    XrSwapchain swapchain = XR_NULL_HANDLE;
    uint32_t swapchainImageCount = 0;
    std::vector<GLuint> framebuffers;
    uint32_t width = 512;
    uint32_t height = 512;
};

void pollEvents(AppState* appState);
void renderFrame(AppState* appState);

void android_main(struct android_app* app) {
    LOGI("Green Overlay app starting up.");

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, nullptr, nullptr);
    EGLConfig config;
    const EGLint configAttribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_ALPHA_SIZE, 8,
            EGL_NONE
    };
    EGLint numConfigs;
    eglChooseConfig(display, configAttribs, &config, 1, &numConfigs);
    const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);

    AppState appState = {};
    appState.app = app;
    app->userData = &appState;
    app->onAppCmd = [](struct android_app* app, int32_t cmd) {
        auto* state = (AppState*)app->userData;
        if (cmd == APP_CMD_RESUME) state->resumed = true;
        if (cmd == APP_CMD_PAUSE) state->resumed = false;
    };

    PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR;
    xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&xrInitializeLoaderKHR);
    XrLoaderInitInfoAndroidKHR loaderInitInfo = {XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    loaderInitInfo.applicationVM = app->activity->vm;
    loaderInitInfo.applicationContext = app->activity->clazz;
    xrInitializeLoaderKHR((const XrLoaderInitInfoBaseHeaderKHR*)&loaderInitInfo);

    uint32_t extensionCount = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
    std::vector<XrExtensionProperties> extensions(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data());

    bool overlaySupported = false;
    for (const auto& ext : extensions) {
        if (strcmp(ext.extensionName, XR_EXTX_OVERLAY_EXTENSION_NAME) == 0) {
            overlaySupported = true;
            break;
        }
    }

    if (!overlaySupported) {
        LOGE("XR_EXTX_overlay extension is NOT SUPPORTED by the runtime!");
        return;
    }
    LOGI("XR_EXTX_overlay extension is supported.");

    std::vector<const char*> instanceExtensions = {
            XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
            XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
            XR_EXTX_OVERLAY_EXTENSION_NAME
    };

    XrApplicationInfo appInfo = {};
    strcpy(appInfo.applicationName, "OverlayAppGreen");
    appInfo.apiVersion = XR_CURRENT_API_VERSION;

    XrInstanceCreateInfoAndroidKHR instanceCreateInfoAndroid = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    instanceCreateInfoAndroid.applicationVM = app->activity->vm;
    instanceCreateInfoAndroid.applicationActivity = app->activity->clazz;

    XrInstanceCreateInfo instanceCreateInfo = {XR_TYPE_INSTANCE_CREATE_INFO};
    instanceCreateInfo.next = &instanceCreateInfoAndroid;
    instanceCreateInfo.applicationInfo = appInfo;
    instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
    instanceCreateInfo.enabledExtensionNames = instanceExtensions.data();

    if (XR_FAILED(xrCreateInstance(&instanceCreateInfo, &appState.instance))) {
        LOGE("xrCreateInstance failed");
        return;
    }

    XrSystemGetInfo systemGetInfo = {XR_TYPE_SYSTEM_GET_INFO, nullptr, XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY};
    if (XR_FAILED(xrGetSystem(appState.instance, &systemGetInfo, &appState.systemId))) {
        LOGE("xrGetSystem failed");
        return;
    }

    uint32_t viewConfigCount;
    xrEnumerateViewConfigurations(appState.instance, appState.systemId, 0, &viewConfigCount, nullptr);
    std::vector<XrViewConfigurationType> viewConfigs(viewConfigCount);
    xrEnumerateViewConfigurations(appState.instance, appState.systemId, viewConfigCount, &viewConfigCount, viewConfigs.data());
    appState.viewConfigType = viewConfigs[0];

    uint32_t blendModeCount;
    xrEnumerateEnvironmentBlendModes(appState.instance, appState.systemId, appState.viewConfigType, 0, &blendModeCount, nullptr);
    std::vector<XrEnvironmentBlendMode> blendModes(blendModeCount);
    xrEnumerateEnvironmentBlendModes(appState.instance, appState.systemId, appState.viewConfigType, blendModeCount, &blendModeCount, blendModes.data());

    bool blendModeSet = false;
    for (XrEnvironmentBlendMode mode : blendModes) {
        if (mode == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND) {
            appState.blendMode = XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
            LOGI("ALPHA_BLEND mode is supported and will be used.");
            blendModeSet = true;
            break;
        }
    }
    if (!blendModeSet) {
        LOGE("ALPHA_BLEND not supported! Overlay may not be transparent.");
        appState.blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    }

    PFN_xrGetOpenGLESGraphicsRequirementsKHR pfnGetReqs;
    xrGetInstanceProcAddr(appState.instance, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction*)&pfnGetReqs);
    XrGraphicsRequirementsOpenGLESKHR graphicsRequirements = {XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    pfnGetReqs(appState.instance, appState.systemId, &graphicsRequirements);

    XrGraphicsBindingOpenGLESAndroidKHR graphicsBinding = {XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    graphicsBinding.display = eglGetCurrentDisplay();
    graphicsBinding.context = eglGetCurrentContext();

    XrSessionCreateInfoOverlayEXTX overlayInfo = {XR_TYPE_SESSION_CREATE_INFO_OVERLAY_EXTX};
    overlayInfo.next = &graphicsBinding;
    overlayInfo.createFlags = 0;
    overlayInfo.sessionLayersPlacement = XR_SESSION_LAYERS_PLACEMENT_OVERLAY_EXTX;

    XrSessionCreateInfo sessionCreateInfo = {XR_TYPE_SESSION_CREATE_INFO};
    sessionCreateInfo.next = &overlayInfo;
    sessionCreateInfo.systemId = appState.systemId;

    if (XR_FAILED(xrCreateSession(appState.instance, &sessionCreateInfo, &appState.session))) {
        LOGE("Overlay session creation failed!");
        return;
    }
    LOGI("Overlay session created successfully.");

    XrReferenceSpaceCreateInfo spaceCreateInfo = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceCreateInfo.poseInReferenceSpace = {{0,0,0,1}, {0,0,0}};
    xrCreateReferenceSpace(appState.session, &spaceCreateInfo, &appState.appSpace);

    XrSwapchainCreateInfo swapchainCreateInfo = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainCreateInfo.format = GL_SRGB8_ALPHA8;
    swapchainCreateInfo.width = appState.width;
    swapchainCreateInfo.height = appState.height;
    swapchainCreateInfo.sampleCount = 1;
    swapchainCreateInfo.faceCount = 1;
    swapchainCreateInfo.arraySize = 1;
    swapchainCreateInfo.mipCount = 1;
    xrCreateSwapchain(appState.session, &swapchainCreateInfo, &appState.swapchain);

    xrEnumerateSwapchainImages(appState.swapchain, 0, &appState.swapchainImageCount, nullptr);
    std::vector<XrSwapchainImageOpenGLESKHR> images(appState.swapchainImageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
    xrEnumerateSwapchainImages(appState.swapchain, appState.swapchainImageCount, &appState.swapchainImageCount, (XrSwapchainImageBaseHeader*)images.data());

    appState.framebuffers.resize(appState.swapchainImageCount);
    glGenFramebuffers(static_cast<GLsizei>(appState.swapchainImageCount), appState.framebuffers.data());
    for (uint32_t i = 0; i < appState.swapchainImageCount; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, appState.framebuffers[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, images[i].image, 0);
    }

    LOGI("Green Overlay App initialized successfully");

    while (!app->destroyRequested) {
        struct android_poll_source* source;
        while (ALooper_pollOnce(0, nullptr, nullptr, (void**)&source) >= 0) {
            if (source) source->process(app, source);
        }
        pollEvents(&appState);
        renderFrame(&appState);
    }
}

void pollEvents(AppState* appState) {
    XrEventDataBuffer eventData = {XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(appState->instance, &eventData) == XR_SUCCESS) {
        if (eventData.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto stateEvent = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&eventData);
            appState->sessionState = stateEvent.state;
            LOGI("Green Overlay session state changed to: %d", appState->sessionState);

            if (appState->sessionState == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo beginInfo = {XR_TYPE_SESSION_BEGIN_INFO};
                beginInfo.primaryViewConfigurationType = appState->viewConfigType;
                if (XR_SUCCEEDED(xrBeginSession(appState->session, &beginInfo))) {
                    appState->sessionRunning = true;
                    LOGI("Green Overlay session started successfully");
                }
            } else if (appState->sessionState == XR_SESSION_STATE_STOPPING) {
                appState->sessionRunning = false;
                xrEndSession(appState->session);
                LOGI("Green Overlay session ended");
            }
        }
        eventData = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

void renderFrame(AppState* appState) {
    if (!appState->sessionRunning || !appState->resumed) return;

    XrFrameState frameState = {XR_TYPE_FRAME_STATE};
    xrWaitFrame(appState->session, nullptr, &frameState);
    xrBeginFrame(appState->session, nullptr);

    const XrCompositionLayerBaseHeader* layerPtr = nullptr;
    static XrCompositionLayerQuad compositionLayer;

    if (frameState.shouldRender) {
        uint32_t imageIndex;
        xrAcquireSwapchainImage(appState->swapchain, nullptr, &imageIndex);
        XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, nullptr, XR_INFINITE_DURATION};
        xrWaitSwapchainImage(appState->swapchain, &waitInfo);

        glBindFramebuffer(GL_FRAMEBUFFER, appState->framebuffers[imageIndex]);
        glViewport(0, 0, appState->width, appState->height);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f); // Clear with transparent
        glClear(GL_COLOR_BUFFER_BIT);

        glBindFramebuffer(GL_FRAMEBUFFER, appState->framebuffers[imageIndex]);
        // Set clear color to GREEN
        glClearColor(0.1f, 0.8f, 0.2f, 0.8f);
        glClear(GL_COLOR_BUFFER_BIT);

        xrReleaseSwapchainImage(appState->swapchain, nullptr);

        compositionLayer = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        compositionLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        compositionLayer.space = appState->appSpace;
        compositionLayer.subImage = {{appState->swapchain}, {{0,0}, {(int32_t)appState->width, (int32_t)appState->height}}};

        // V V V EDITED THIS LINE V V V

        // To change POSITION, edit the {X, Y, Z} values here.
        // X: 0.6f keeps it on the right side.
        // Y: 0.3f moves it up.
        // Z: -1.2f keeps it at the same distance.
        compositionLayer.pose = {{0,0,0,1}, {0.2f, 0.5f, -1.2f}}; // MOVED UP AND TO THE RIGHT

        // To change DIMENSIONS, edit the {width, height} values here.
        compositionLayer.size = {0.5f, 0.5f}; // EXAMPLE: Changed back to a square

        // ^ ^ ^ EDITED THIS LINE ^ ^ ^

        layerPtr = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&compositionLayer);
    }

    XrFrameEndInfo endInfo = {XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = appState->blendMode;
    endInfo.layerCount = (layerPtr ? 1 : 0);
    endInfo.layers = (layerPtr ? &layerPtr : nullptr);
    xrEndFrame(appState->session, &endInfo);
}

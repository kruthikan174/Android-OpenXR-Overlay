#include <android/log.h>
#include "android_native_app_glue.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <memory>
#include <vector>
#include <cstring>
#include <jni.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <chrono>

// =======================
// FPS Globals
// =======================

// Frame counter
static int g_frameCount = 0;

// Frames per second value
static float g_fps = 0.0f;

// Last recorded time point (for FPS calculation)
static std::chrono::time_point<std::chrono::high_resolution_clock> g_lastTimePoint =
        std::chrono::high_resolution_clock::now();

#define TAG "BaseApp"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

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
    XrEnvironmentBlendMode blendMode;

    XrSwapchain swapchain = XR_NULL_HANDLE;
    uint32_t swapchainImageCount = 0;
    std::vector<GLuint> framebuffers;
    uint32_t width = 1024;
    uint32_t height = 1024;
};

// Global pointer to the application state
static AppState* g_appState = nullptr;

// JNI function to be called from Java
extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_androidsamsung_MainActivity_isSessionFocused(JNIEnv* env, jobject thiz) {
    if (g_appState == nullptr) {
        return false;
    }
    return g_appState->sessionState == XR_SESSION_STATE_FOCUSED;
}


void pollEvents(AppState* appState) {
    XrEventDataBuffer eventData = {XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(appState->instance, &eventData) == XR_SUCCESS) {
        if (eventData.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto stateEvent = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&eventData);
            appState->sessionState = stateEvent.state;
            LOGI("BaseApp session state changed to: %d", stateEvent.state);

            switch (appState->sessionState) {
                case XR_SESSION_STATE_READY: {
                    XrSessionBeginInfo beginInfo = {XR_TYPE_SESSION_BEGIN_INFO};
                    beginInfo.primaryViewConfigurationType = appState->viewConfigType;
                    if (XR_SUCCEEDED(xrBeginSession(appState->session, &beginInfo))) {
                        appState->sessionRunning = true;
                        LOGI("Base session has begun (is running)");
                    } else {
                        LOGE("Failed to begin base session!");
                    }
                    break;
                }
                case XR_SESSION_STATE_STOPPING: {
                    appState->sessionRunning = false;
                    LOGI("Base session is stopping (focus lost). NOT calling xrEndSession.");
                    break;
                }
                case XR_SESSION_STATE_FOCUSED: {
                    LOGI("Base session is FOCUSED. Ready for overlay.");
                    break;
                }
                case XR_SESSION_STATE_VISIBLE: {
                    LOGI("Base session is VISIBLE.");
                    break;
                }
                case XR_SESSION_STATE_SYNCHRONIZED: {
                    LOGI("Base session is SYNCHRONIZED.");
                    break;
                }
                case XR_SESSION_STATE_IDLE: {
                    LOGI("Base session is IDLE.");
                    break;
                }
                case XR_SESSION_STATE_EXITING: {
                    LOGI("Base session is EXITING. Calling xrEndSession.");
                    appState->sessionRunning = false;
                    xrEndSession(appState->session);
                    // Also finish the activity to ensure a clean exit
                    ANativeActivity_finish(appState->app->activity);
                    break;
                }
                default:
                    break;
            }
        }
        eventData = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

void renderFrame(AppState* appState) {
    if (!appState->sessionRunning) {
        return;
    }

    XrFrameState frameState = {XR_TYPE_FRAME_STATE};
    XrFrameWaitInfo frameWaitInfo = {XR_TYPE_FRAME_WAIT_INFO};
    if (XR_FAILED(xrWaitFrame(appState->session, &frameWaitInfo, &frameState))) {
        LOGI("xrWaitFrame failed, likely because session is not focused.");
        return;
    }

    XrFrameBeginInfo frameBeginInfo = {XR_TYPE_FRAME_BEGIN_INFO};
    if (XR_FAILED(xrBeginFrame(appState->session, &frameBeginInfo))) {
        LOGE("xrBeginFrame failed");
        return;
    }

    const XrCompositionLayerBaseHeader* layerPtr = nullptr;
    XrCompositionLayerProjection projectionLayer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    std::vector<XrCompositionLayerProjectionView> projectionViews;

    if (frameState.shouldRender) {
        uint32_t imageIndex;
        XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        xrAcquireSwapchainImage(appState->swapchain, &acquireInfo, &imageIndex);

        XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        waitInfo.timeout = XR_INFINITE_DURATION;
        xrWaitSwapchainImage(appState->swapchain, &waitInfo);

        glBindFramebuffer(GL_FRAMEBUFFER, appState->framebuffers[imageIndex]);
        glViewport(0, 0, appState->width, appState->height);
        // Changed from dark green to cyan
        glClearColor(0.0f, 0.4f, 0.4f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(appState->swapchain, &releaseInfo);

        XrViewLocateInfo viewLocateInfo = {XR_TYPE_VIEW_LOCATE_INFO};
        viewLocateInfo.viewConfigurationType = appState->viewConfigType;
        viewLocateInfo.displayTime = frameState.predictedDisplayTime;
        viewLocateInfo.space = appState->appSpace;

        uint32_t viewCount = 2;
        std::vector<XrView> views(viewCount, {XR_TYPE_VIEW});
        XrViewState viewState = {XR_TYPE_VIEW_STATE};
        xrLocateViews(appState->session, &viewLocateInfo, &viewState, viewCount, &viewCount, views.data());

        projectionViews.resize(viewCount);
        for(uint32_t i = 0; i < viewCount; ++i) {
            projectionViews[i] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
            projectionViews[i].pose = views[i].pose;
            projectionViews[i].fov = views[i].fov;
            projectionViews[i].subImage.swapchain = appState->swapchain;
            projectionViews[i].subImage.imageRect.offset = {0, 0};
            projectionViews[i].subImage.imageRect.extent = {(int32_t)appState->width, (int32_t)appState->height};
        }

        projectionLayer.space = appState->appSpace;
        projectionLayer.viewCount = viewCount;
        projectionLayer.views = projectionViews.data();
        layerPtr = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projectionLayer);
    }

    XrFrameEndInfo endInfo = {XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = appState->blendMode;
    endInfo.layerCount = (layerPtr ? 1 : 0);
    endInfo.layers = &layerPtr;
    xrEndFrame(appState->session, &endInfo);

    // --- FPS calculation (only count actual rendered frames) ---
    g_frameCount++;
    auto now = std::chrono::high_resolution_clock::now();
    double elapsedSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(now - g_lastTimePoint).count();

    if (elapsedSeconds >= 1.0) {
        g_fps = static_cast<float>(g_frameCount) / elapsedSeconds;
        g_frameCount = 0;
        g_lastTimePoint = now;
        LOGI("FPS: %.2f", g_fps);   // now will be ~60, 72, 90, depending on HMD refresh
    }
    // --- End FPS calculation ---
}

void android_main(struct android_app* app) {
    AppState appState = {};
    app->userData = &appState;
    g_appState = &appState;
    appState.app = app;

    app->onAppCmd = [](struct android_app* app, int32_t cmd) {
        auto* state = (AppState*)app->userData;
        switch (cmd) {
            case APP_CMD_RESUME:
                state->resumed = true;
                LOGI("BaseApp got APP_CMD_RESUME");
                break;
            case APP_CMD_PAUSE:
                state->resumed = false;
                LOGI("BaseApp got APP_CMD_PAUSE");
                break;
            case APP_CMD_TERM_WINDOW:
                // *** CRITICAL FIX ***
                // The window is being destroyed, so we should exit the app.
                LOGI("BaseApp got APP_CMD_TERM_WINDOW. Finishing activity.");
                ANativeActivity_finish(app->activity);
                break;
            case APP_CMD_DESTROY:
                // The app is being destroyed. The main loop will exit.
                LOGI("BaseApp got APP_CMD_DESTROY.");
                break;
        }
    };

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, nullptr, nullptr);
    EGLConfig config;
    const EGLint configAttribs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE };
    EGLint numConfigs;
    eglChooseConfig(display, configAttribs, &config, 1, &numConfigs);
    const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);

    PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR;
    xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&xrInitializeLoaderKHR);
    XrLoaderInitInfoAndroidKHR loaderInitInfo = {XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    loaderInitInfo.applicationVM = app->activity->vm;
    loaderInitInfo.applicationContext = app->activity->clazz;
    xrInitializeLoaderKHR((const XrLoaderInitInfoBaseHeaderKHR*)&loaderInitInfo);

    std::vector<const char*> extensions = {
            XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
            XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME
    };
    XrApplicationInfo appInfo = {};
    strcpy(appInfo.applicationName, "BaseApp");
    appInfo.apiVersion = XR_CURRENT_API_VERSION;

    XrInstanceCreateInfoAndroidKHR instanceCreateInfoAndroid = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    instanceCreateInfoAndroid.applicationVM = app->activity->vm;
    instanceCreateInfoAndroid.applicationActivity = app->activity->clazz;

    XrInstanceCreateInfo instanceCreateInfo = {XR_TYPE_INSTANCE_CREATE_INFO};
    instanceCreateInfo.next = &instanceCreateInfoAndroid;
    instanceCreateInfo.applicationInfo = appInfo;
    instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instanceCreateInfo.enabledExtensionNames = extensions.data();

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
    appState.blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

    PFN_xrGetOpenGLESGraphicsRequirementsKHR pfnGetReqs;
    xrGetInstanceProcAddr(appState.instance, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction*)&pfnGetReqs);
    XrGraphicsRequirementsOpenGLESKHR graphicsRequirements = {XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    pfnGetReqs(appState.instance, appState.systemId, &graphicsRequirements);

    XrGraphicsBindingOpenGLESAndroidKHR graphicsBinding = {XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    graphicsBinding.display = eglGetCurrentDisplay();
    graphicsBinding.context = eglGetCurrentContext();

    XrSessionCreateInfo sessionCreateInfo = {XR_TYPE_SESSION_CREATE_INFO};
    sessionCreateInfo.next = &graphicsBinding;
    sessionCreateInfo.systemId = appState.systemId;

    if (XR_FAILED(xrCreateSession(appState.instance, &sessionCreateInfo, &appState.session))) {
        LOGE("xrCreateSession failed");
        return;
    }

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

    LOGI("Base App initialized successfully");

    while (!app->destroyRequested) {
        struct android_poll_source* source;
        while (ALooper_pollOnce(0, nullptr, nullptr, (void**)&source) >= 0) {
            if (source) source->process(app, source);
        }
        pollEvents(&appState);
        renderFrame(&appState);
    }

    g_appState = nullptr;
}
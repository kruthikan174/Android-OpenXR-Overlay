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

#define TAG "OverlayAppBlue"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// Define overlay extension structures directly to ensure they are available.
#define XR_EXTX_OVERLAY_EXTENSION_NAME "XR_EXTX_overlay"
#define XR_TYPE_SESSION_CREATE_INFO_OVERLAY_EXTX ((XrStructureType) 1000033000)

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

// Helper to load shader source from assets
std::string loadAssetShader(AAssetManager* mgr, const char* filename) {
    AAsset* asset = AAssetManager_open(mgr, filename, AASSET_MODE_BUFFER);
    if (!asset) {
        LOGE("Failed to open shader: %s", filename);
        return "";
    }
    size_t size = AAsset_getLength(asset);
    std::string buffer(size, '\0');
    AAsset_read(asset, buffer.data(), size);
    AAsset_close(asset);
    return buffer;
}


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

struct EyeSwapchain {
    XrSwapchain swapchain = XR_NULL_HANDLE;
    uint32_t imageCount = 0;
    std::vector<XrSwapchainImageOpenGLESKHR> images; // populated after enumerate
    std::vector<GLuint> framebuffers; // GL framebuffer per swapchain image
    std::vector<GLuint> colorTextures; // GL texture ids (if needed)
    std::vector<GLuint> depthRenderbuffers; // depth renderbuffers per image
    uint32_t width = 0;
    uint32_t height = 0;
};

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

    // ✅ Add these new members
    uint32_t viewCount = 0;
    std::vector<XrViewConfigurationView> viewConfigs;
    std::vector<XrView> views;
    std::vector<EyeSwapchain> eyeSwapchains;

    XrSwapchain swapchain = XR_NULL_HANDLE;
    uint32_t swapchainImageCount = 0;

    uint32_t imageCount = 0;
    std::vector<XrSwapchainImageOpenGLESKHR> images; // populated after enumerate
    std::vector<GLuint> framebuffers; // GL framebuffer per swapchain image
    std::vector<GLuint> colorTextures; // GL texture ids (if needed)
    std::vector<GLuint> depthRenderbuffers; // depth renderbuffers per image

    uint32_t width = 1024;
    uint32_t height = 256;

    // 🔹 Add these for your 3D overlay
    GLuint shaderProg = 0;
    GLuint vao = 0;

};

void pollEvents(AppState* appState);
void renderFrame(AppState* appState);



GLuint compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint len;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len);
        glGetShaderInfoLog(shader, len, &len, log.data());
        LOGE("Shader compile error: %s", log.data());
    }
    return shader;
}

GLuint createProgram(const char* vsSrc, const char* fsSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint linked;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint len;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len);
        glGetProgramInfoLog(prog, len, &len, log.data());
        LOGE("Program link error: %s", log.data());
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}
float cubeVertices[] = {
        // positions         // colors
        -0.5f,-0.5f,-0.5f,   1,0,0,
        0.5f,-0.5f,-0.5f,   0,1,0,
        0.5f, 0.5f,-0.5f,   0,0,1,
        -0.5f, 0.5f,-0.5f,   1,1,0,
        -0.5f,-0.5f, 0.5f,   1,0,1,
        0.5f,-0.5f, 0.5f,   0,1,1,
        0.5f, 0.5f, 0.5f,   1,1,1,
        -0.5f, 0.5f, 0.5f,   0.3,0.3,0.3
};

uint16_t cubeIndices[] = {
        0,1,2, 2,3,0,
        4,5,6, 6,7,4,
        0,1,5, 5,4,0,
        2,3,7, 7,6,2,
        0,3,7, 7,4,0,
        1,2,6, 6,5,1
};


void android_main(struct android_app* app) {
    LOGI("Blue Overlay app starting up.");

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

    AAssetManager* mgr = app->activity->assetManager;

    AppState appState = {};
    appState.app = app;
    app->userData = &appState;
    app->onAppCmd = [](struct android_app* app, int32_t cmd) {
        auto* state = (AppState*)app->userData;
        if (cmd == APP_CMD_RESUME) state->resumed = true;
        if (cmd == APP_CMD_PAUSE) state->resumed = false;
    };

    // Load shader sources from assets
    std::string vertexSrc = loadAssetShader(mgr, "vertex_shader.glsl");
    std::string fragmentSrc = loadAssetShader(mgr, "fragment_shader.glsl");

    if (vertexSrc.empty() || fragmentSrc.empty()) {
        LOGE("Failed to load shader files from assets!");
    }

    GLuint shaderProgram = createProgram(vertexSrc.c_str(), fragmentSrc.c_str());
    appState.shaderProg = shaderProgram;

    // ✅ Now initialize buffers (this part you asked about)
    GLuint vao, vbo, ebo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeIndices), cubeIndices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    appState.vao = vao;

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
    strcpy(appInfo.applicationName, "OverlayAppBlue");
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

    // Query viewCount
    xrEnumerateViewConfigurationViews(appState.instance, appState.systemId, appState.viewConfigType, 0, &appState.viewCount, nullptr);
    appState.viewConfigs.resize(appState.viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    xrEnumerateViewConfigurationViews(appState.instance, appState.systemId, appState.viewConfigType,
                                      appState.viewCount, &appState.viewCount, appState.viewConfigs.data());

    // Prepare view structures for xrLocateViews
    appState.views.resize(appState.viewCount, {XR_TYPE_VIEW});
    // Prepare storage for per-eye swapchains
    appState.eyeSwapchains.resize(appState.viewCount);


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
        for (XrEnvironmentBlendMode mode : blendModes) {
            if (mode == XR_ENVIRONMENT_BLEND_MODE_ADDITIVE) {
                appState.blendMode = XR_ENVIRONMENT_BLEND_MODE_ADDITIVE;
                LOGI("ALPHA_BLEND not supported. Falling back to ADDITIVE blend mode.");
                blendModeSet = true;
                break;
            }
        }
    }
    if (!blendModeSet) {
        LOGE("Neither ALPHA_BLEND nor ADDITIVE blend modes are supported! Overlay will be opaque.");
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

    // ---- Create Reference Space (after xrCreateSession) ----
    XrReferenceSpaceCreateInfo spaceCreateInfo = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceCreateInfo.poseInReferenceSpace.orientation = {0, 0, 0, 1};
    spaceCreateInfo.poseInReferenceSpace.position = {0, 0, 0};

    XrResult r = xrCreateReferenceSpace(appState.session, &spaceCreateInfo, &appState.appSpace);
    if (XR_FAILED(r)) {
        LOGE("xrCreateReferenceSpace failed: 0x%X", r);
        // Don’t continue — without a valid space, rendering will fail.
        return;
    }
    LOGI("Reference space created successfully: %p", (void*)appState.appSpace);

    for (uint32_t i = 0; i < appState.viewCount; ++i) {
        EyeSwapchain &eye = appState.eyeSwapchains[i];
        eye.width = appState.viewConfigs[i].recommendedImageRectWidth;
        eye.height = appState.viewConfigs[i].recommendedImageRectHeight;

        XrSwapchainCreateInfo sci = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
        sci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        sci.format = GL_SRGB8_ALPHA8;
        sci.sampleCount = 1;
        sci.width = eye.width;
        sci.height = eye.height;
        sci.mipCount = 1;
        sci.faceCount = 1;
        sci.arraySize = 1;
        sci.createFlags = 0;

        if (XR_FAILED(xrCreateSwapchain(appState.session, &sci, &eye.swapchain))) {
            LOGE("Failed to create swapchain for eye %u", i);
            continue;
        }

        xrEnumerateSwapchainImages(eye.swapchain, 0, &eye.imageCount, nullptr);
        eye.images.resize(eye.imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
        xrEnumerateSwapchainImages(eye.swapchain, eye.imageCount, &eye.imageCount,
                                   (XrSwapchainImageBaseHeader*)eye.images.data());

        eye.framebuffers.resize(eye.imageCount);
        eye.colorTextures.resize(eye.imageCount);
        eye.depthRenderbuffers.resize(eye.imageCount);

        glGenFramebuffers((GLsizei)eye.imageCount, eye.framebuffers.data());
        glGenTextures((GLsizei)eye.imageCount, eye.colorTextures.data());
        glGenRenderbuffers((GLsizei)eye.imageCount, eye.depthRenderbuffers.data());

        for (uint32_t img = 0; img < eye.imageCount; ++img) {
            GLuint tex = eye.images[img].image; // provided by runtime
            glBindFramebuffer(GL_FRAMEBUFFER, eye.framebuffers[img]);

            // Attach color
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

            // Depth renderbuffer
            glBindRenderbuffer(GL_RENDERBUFFER, eye.depthRenderbuffers[img]);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, eye.width, eye.height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, eye.depthRenderbuffers[img]);

            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                LOGE("Framebuffer incomplete for eye %u img %u: 0x%X", i, img, status);
            }
        }

        LOGI("Eye %u swapchain created (%ux%u, %u images)", i, eye.width, eye.height, eye.imageCount);
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


    LOGI("Blue Overlay App initialized successfully");

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
            LOGI("Blue Overlay session state changed to: %d", appState->sessionState);

            if (appState->sessionState == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo beginInfo = {XR_TYPE_SESSION_BEGIN_INFO};
                beginInfo.primaryViewConfigurationType = appState->viewConfigType;
                if (XR_SUCCEEDED(xrBeginSession(appState->session, &beginInfo))) {
                    appState->sessionRunning = true;
                    LOGI("Blue Overlay session started successfully");
                }
            } else if (appState->sessionState == XR_SESSION_STATE_STOPPING) {
                appState->sessionRunning = false;
                xrEndSession(appState->session);
                LOGI("Blue Overlay session ended");
            }
        }
        eventData = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

void renderFrame(AppState* appState) {
    if (!appState->sessionRunning || !appState->resumed) return;

    XrFrameState frameState = {XR_TYPE_FRAME_STATE};
    XrResult r = xrWaitFrame(appState->session, nullptr, &frameState);
    if (XR_FAILED(r)) {
        LOGE("xrWaitFrame failed: 0x%X", r);
        return;
    }

    r = xrBeginFrame(appState->session, nullptr);
    if (XR_FAILED(r)) {
        LOGE("xrBeginFrame failed: 0x%X", r);
        return;
    }

    bool haveLayer = false;
    static XrCompositionLayerProjection projectionLayer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    std::vector<XrCompositionLayerProjectionView> projectionLayerViews(appState->viewCount,
                                                                       {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW});
    std::vector<XrView>& views = appState->views;

    if (frameState.shouldRender) {
        // Locate views
        XrViewLocateInfo viewLocateInfo = {XR_TYPE_VIEW_LOCATE_INFO};
        viewLocateInfo.viewConfigurationType = appState->viewConfigType;
        viewLocateInfo.displayTime = frameState.predictedDisplayTime;
        viewLocateInfo.space = appState->appSpace;

        XrViewState viewState = {XR_TYPE_VIEW_STATE};
        uint32_t viewCountOutput = 0;
        r = xrLocateViews(appState->session, &viewLocateInfo, &viewState,
                          appState->viewCount, &viewCountOutput, views.data());
        if (XR_FAILED(r)) {
            LOGE("xrLocateViews failed: 0x%X", r);
            // Continue but don't present a layer
        } else {
            // Render each eye
            for (uint32_t eye = 0; eye < appState->viewCount; ++eye) {
                EyeSwapchain &eyeSc = appState->eyeSwapchains[eye];
                uint32_t imageIndex = 0;
                r = xrAcquireSwapchainImage(eyeSc.swapchain, nullptr, &imageIndex);
                if (XR_FAILED(r)) {
                    LOGE("xrAcquireSwapchainImage failed for eye %u: 0x%X", eye, r);
                    continue;
                }

                XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, nullptr, XR_INFINITE_DURATION};
                r = xrWaitSwapchainImage(eyeSc.swapchain, &waitInfo);
                if (XR_FAILED(r)) {
                    LOGE("xrWaitSwapchainImage failed for eye %u: 0x%X", eye, r);
                    // still try to release to be safe
                    xrReleaseSwapchainImage(eyeSc.swapchain, nullptr);
                    continue;
                }

                // Bind GL framebuffer that uses runtime-provided texture
                glBindFramebuffer(GL_FRAMEBUFFER, eyeSc.framebuffers[imageIndex]);
                glViewport(0, 0, eyeSc.width, eyeSc.height);
                glClearColor(0.0f, 0.0f, 0.5f, 0.1f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                glUseProgram(appState->shaderProg);
                glBindVertexArray(appState->vao);

            // Simple MVP rotation matrix
                float t = (float)(frameState.predictedDisplayTime % 1000000000LL) / 1e9f;
                float angle = t * 1.5f; // rotation speed

                float mvp[16] = {
                        cos(angle), 0,  sin(angle), 0,
                        0,          1,  0,          0,
                        -sin(angle), 0,  cos(angle), -2.5f,
                        0,          0,  0,          1
                };


                GLint loc = glGetUniformLocation(appState->shaderProg, "uMVP");
                glUniformMatrix4fv(loc, 1, GL_FALSE, mvp);

                glDrawElements(GL_TRIANGLES, sizeof(cubeIndices)/sizeof(cubeIndices[0]), GL_UNSIGNED_SHORT, 0);

                // TODO: convert xr pose/fov to GL matrices and draw your scene here.

                // Release swapchain image
                r = xrReleaseSwapchainImage(eyeSc.swapchain, nullptr);
                if (XR_FAILED(r)) {
                    LOGE("xrReleaseSwapchainImage failed for eye %u: 0x%X", eye, r);
                }

                // Fill projectionLayerViews for this eye
                projectionLayerViews[eye].pose = views[eye].pose;
                projectionLayerViews[eye].fov = views[eye].fov;
                projectionLayerViews[eye].subImage.swapchain = eyeSc.swapchain;
                projectionLayerViews[eye].subImage.imageArrayIndex = 0;
                projectionLayerViews[eye].subImage.imageRect.offset = {0, 0};
                projectionLayerViews[eye].subImage.imageRect.extent = { (int32_t)eyeSc.width, (int32_t)eyeSc.height };
            }

            // Only set up the projection layer if at least one view had a valid swapchain
            projectionLayer.space = appState->appSpace;
            projectionLayer.viewCount = (uint32_t)projectionLayerViews.size();
            projectionLayer.views = projectionLayerViews.data();
            haveLayer = true;
        }
    }

    // Prepare end info — only include layers if we actually have one
    XrFrameEndInfo endInfo = {XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = appState->blendMode;


    if (haveLayer) {
        const XrCompositionLayerBaseHeader* layers[] = {
                reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projectionLayer)
        };
        endInfo.layerCount = 1;
        endInfo.layers = layers;
    } else {
        endInfo.layerCount = 0;
        endInfo.layers = nullptr;
    }

    r = xrEndFrame(appState->session, &endInfo);
    if (XR_FAILED(r)) {
        LOGE("xrEndFrame failed: 0x%X", r);
    }
}



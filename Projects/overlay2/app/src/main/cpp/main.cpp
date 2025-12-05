#include <android/log.h>
#include "android_native_app_glue.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/input.h> // Include for input event handling

// =================================================================================================
// --- Mobile Test Mode Switch ---
// Define this to build for a standard Android phone/emulator. The app will render directly
// to the screen and skip all OpenXR calls.
// Comment this line out to build for a real VR headset.
//#define TEST_ON_MOBILE
// =================================================================================================


#if !defined(TEST_ON_MOBILE)
// XR_USE_PLATFORM_ANDROID and XR_USE_GRAPHICS_API_OPENGL_ES must be defined before including openxr headers
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include "openxr/include/openxr/openxr.h"
#include "openxr/include/openxr/openxr_platform.h"
#endif

#include <vector>
#include <string>
#include <cmath>
#include <cstring>
#include <unistd.h>
#include <array>

#define LOG_TAG "XR_App_Test"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// --- Globals ---
EGLDisplay eglDisplay = EGL_NO_DISPLAY;
EGLContext eglContext = EGL_NO_CONTEXT;
EGLConfig eglConfig;

// Mobile-only globals
EGLSurface eglSurface = EGL_NO_SURFACE;
int32_t windowWidth = 0;
int32_t windowHeight = 0;

#if !defined(TEST_ON_MOBILE)
// OpenXR handles
XrInstance instance = XR_NULL_HANDLE;
XrSystemId systemId = XR_NULL_SYSTEM_ID;
XrSession session = XR_NULL_HANDLE;
XrSpace appSpace = XR_NULL_HANDLE;
XrSwapchain swapchain = XR_NULL_HANDLE;

// Swapchain images
struct SwapchainImage {
    XrSwapchainImageOpenGLESKHR khr;
};
std::vector<SwapchainImage> swapchainImages;

// Framebuffer for rendering to swapchain images
struct Framebuffer {
    GLuint framebuffer = 0;
    GLuint depthbuffer = 0;
};
Framebuffer renderFramebuffer;

// App state
bool sessionRunning = false;
XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;

// View configuration
std::vector<XrViewConfigurationView> viewConfigViews;
std::vector<XrView> views;
std::vector<XrCompositionLayerProjectionView> projectionViews;
#endif

// Simple vertex shader
const char* vertexShaderSource = R"(#version 300 es
layout (location = 0) in vec3 aPos;
uniform mat4 mvp;
void main() {
    gl_Position = mvp * vec4(aPos, 1.0);
}
)";

// Simple fragment shader for background
const char* fragmentShaderSource = R"(#version 300 es
precision mediump float;
uniform vec3 color;
out vec4 FragColor;
void main() {
    FragColor = vec4(color, 1.0);
}
)";

// Fragment shader for overlay (with transparency)
const char* overlayFragmentShaderSource = R"(#version 300 es
precision mediump float;
uniform vec3 color;
uniform float alpha;
out vec4 FragColor;
void main() {
    FragColor = vec4(color, alpha);
}
)";

GLuint shaderProgram = 0;
GLuint overlayShaderProgram = 0;
GLuint VAO = 0;
GLuint VBO = 0;

// --- Matrix Math ---
void matrix_identity(float* m) {
    m[0] = 1; m[4] = 0; m[8] = 0;  m[12] = 0;
    m[1] = 0; m[5] = 1; m[9] = 0;  m[13] = 0;
    m[2] = 0; m[6] = 0; m[10] = 1; m[14] = 0;
    m[3] = 0; m[7] = 0; m[11] = 0; m[15] = 1;
}

void matrix_multiply(const float* a, const float* b, float* r) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            r[i * 4 + j] = 0;
            for (int k = 0; k < 4; ++k) {
                r[i * 4 + j] += a[k * 4 + j] * b[i * 4 + k];
            }
        }
    }
}

void matrix_translate(float x, float y, float z, float* m) {
    matrix_identity(m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

void matrix_scale(float sx, float sy, float sz, float* m) {
    matrix_identity(m);
    m[0] = sx;
    m[5] = sy;
    m[10] = sz;
}


#if !defined(TEST_ON_MOBILE)
void matrix_create_projection_from_fov(const XrFovf& fov, float nearZ, float farZ, float* m) {
    const float tan_left = tanf(fov.angleLeft);
    const float tan_right = tanf(fov.angleRight);
    const float tan_down = tanf(fov.angleDown);
    const float tan_up = tanf(fov.angleUp);
    const float tan_width = tan_right - tan_left;
    const float tan_height = tan_up - tan_down;
    m[0] = 2.0f / tan_width;
    m[1] = 0.0f;
    m[2] = 0.0f;
    m[3] = 0.0f;
    m[4] = 0.0f;
    m[5] = 2.0f / tan_height;
    m[6] = 0.0f;
    m[7] = 0.0f;
    m[8] = (tan_right + tan_left) / tan_width;
    m[9] = (tan_up + tan_down) / tan_height;
    m[10] = -(farZ + nearZ) / (farZ - nearZ);
    m[11] = -1.0f;
    m[12] = 0.0f;
    m[13] = 0.0f;
    m[14] = -2.0f * farZ * nearZ / (farZ - nearZ);
    m[15] = 0.0f;
}

void matrix_create_view_from_pose(const XrPosef& pose, float* m) {
    const XrQuaternionf& q = pose.orientation;
    const XrVector3f& p = pose.position;
    float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
    float xx = q.x * x2, xy = q.x * y2, xz = q.x * z2;
    float yy = q.y * y2, yz = q.y * z2, zz = q.z * z2;
    float wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;
    m[0] = 1 - (yy + zz); m[4] = xy - wz;     m[8] = xz + wy;      m[12] = -(m[0] * p.x + m[4] * p.y + m[8] * p.z);
    m[1] = xy + wz;      m[5] = 1 - (xx + zz); m[9] = yz - wx;      m[13] = -(m[1] * p.x + m[5] * p.y + m[9] * p.z);
    m[2] = xz - wy;      m[6] = yz + wx;      m[10] = 1 - (xx + yy); m[14] = -(m[2] * p.x + m[6] * p.y + m[10] * p.z);
    m[3] = 0;            m[7] = 0;            m[11] = 0;             m[15] = 1;
}
#endif


// --- Initialization and Cleanup ---

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        LOGE("Shader compilation failed: %s", infoLog);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool initOpenGL() {
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint overlayFragmentShader = compileShader(GL_FRAGMENT_SHADER, overlayFragmentShaderSource);
    overlayShaderProgram = glCreateProgram();
    glAttachShader(overlayShaderProgram, vertexShader);
    glAttachShader(overlayShaderProgram, overlayFragmentShader);
    glLinkProgram(overlayShaderProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(overlayFragmentShader);

    float vertices[] = {
            -0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.5f,  0.5f, 0.0f, -0.5f,  0.5f, 0.0f,
    };
    unsigned int indices[] = { 0, 1, 2, 2, 3, 0 };

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    GLuint EBO;
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    LOGI("OpenGL base initialized successfully");
    return true;
}

bool initEGL(android_app* app) {
    eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(eglDisplay, nullptr, nullptr);

    EGLint configAttribs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE };
    EGLint numConfigs;
    eglChooseConfig(eglDisplay, configAttribs, &eglConfig, 1, &numConfigs);

    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, contextAttribs);

#if defined(TEST_ON_MOBILE)
    // For mobile, create a surface from the app's window
    eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, app->window, nullptr);
#else
    // For VR, create a placeholder Pbuffer surface
    const EGLint pbufferAttribs[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
    eglSurface = eglCreatePbufferSurface(eglDisplay, eglConfig, pbufferAttribs);
#endif

    eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);

#if defined(TEST_ON_MOBILE)
    eglQuerySurface(eglDisplay, eglSurface, EGL_WIDTH, &windowWidth);
    eglQuerySurface(eglDisplay, eglSurface, EGL_HEIGHT, &windowHeight);
#endif

    LOGI("EGL initialized successfully");
    return true;
}

void cleanup() {
    LOGI("Starting cleanup");

#if !defined(TEST_ON_MOBILE)
    if (sessionRunning) xrEndSession(session);
    if (renderFramebuffer.framebuffer) glDeleteFramebuffers(1, &renderFramebuffer.framebuffer);
    if (renderFramebuffer.depthbuffer) glDeleteRenderbuffers(1, &renderFramebuffer.depthbuffer);
#endif

    if (VAO) glDeleteVertexArrays(1, &VAO);
    if (VBO) glDeleteBuffers(1, &VBO);
    if (shaderProgram) glDeleteProgram(shaderProgram);
    if (overlayShaderProgram) glDeleteProgram(overlayShaderProgram);

#if !defined(TEST_ON_MOBILE)
    if (swapchain) xrDestroySwapchain(swapchain);
    if (appSpace) xrDestroySpace(appSpace);
    if (session) xrDestroySession(session);
    if (instance) xrDestroyInstance(instance);
#endif

    if (eglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (eglSurface != EGL_NO_SURFACE) eglDestroySurface(eglDisplay, eglSurface);
        if (eglContext != EGL_NO_CONTEXT) eglDestroyContext(eglDisplay, eglContext);
        eglTerminate(eglDisplay);
    }
    LOGI("Cleanup completed");
}

#if !defined(TEST_ON_MOBILE)
// --- VR-ONLY FUNCTIONS ---

bool initOpenXR(android_app* app) {
    const char* extensions[] = { XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME };
    XrInstanceCreateInfoAndroidKHR androidInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    androidInfo.applicationVM = app->activity->vm;
    androidInfo.applicationActivity = app->activity->clazz;

    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.next = &androidInfo;
    createInfo.enabledExtensionCount = sizeof(extensions) / sizeof(extensions[0]);
    createInfo.enabledExtensionNames = extensions;
    strcpy(createInfo.applicationInfo.applicationName, "OpenXR Overlay Demo");
    createInfo.applicationInfo.applicationVersion = 1;
    strcpy(createInfo.applicationInfo.engineName, "Custom Engine");
    createInfo.applicationInfo.engineVersion = 1;
    createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;

    if (XR_FAILED(xrCreateInstance(&createInfo, &instance))) {
        LOGE("Failed to create OpenXR instance");
        return false;
    }

    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO, nullptr, XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY};
    if (XR_FAILED(xrGetSystem(instance, &systemInfo, &systemId))) {
        LOGE("Failed to get OpenXR system");
        return false;
    }

    uint32_t viewCount;
    xrEnumerateViewConfigurationViews(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
    viewConfigViews.resize(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    views.resize(viewCount, {XR_TYPE_VIEW});
    projectionViews.resize(viewCount);
    xrEnumerateViewConfigurationViews(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, viewConfigViews.data());

    XrGraphicsBindingOpenGLESAndroidKHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    graphicsBinding.display = eglDisplay;
    graphicsBinding.config = eglConfig;
    graphicsBinding.context = eglContext;

    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO, &graphicsBinding, 0, systemId};
    if (XR_FAILED(xrCreateSession(instance, &sessionInfo, &session))) {
        LOGE("Failed to create OpenXR session");
        return false;
    }

    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO, nullptr, XR_REFERENCE_SPACE_TYPE_VIEW, {{0,0,0,1},{0,0,0}}};
    if (XR_FAILED(xrCreateReferenceSpace(session, &spaceInfo, &appSpace))) {
        LOGE("Failed to create reference space");
        return false;
    }

    XrSwapchainCreateInfo swapchainInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.format = GL_RGBA8;
    swapchainInfo.sampleCount = 1;
    swapchainInfo.width = viewConfigViews[0].recommendedImageRectWidth;
    swapchainInfo.height = viewConfigViews[0].recommendedImageRectHeight;
    swapchainInfo.faceCount = 1;
    swapchainInfo.arraySize = viewCount;
    swapchainInfo.mipCount = 1;

    if (XR_FAILED(xrCreateSwapchain(session, &swapchainInfo, &swapchain))) {
        LOGE("Failed to create swapchain");
        return false;
    }

    uint32_t imageCount;
    xrEnumerateSwapchainImages(swapchain, 0, &imageCount, nullptr);
    swapchainImages.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++) {
        swapchainImages[i].khr = {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR};
    }
    xrEnumerateSwapchainImages(swapchain, imageCount, &imageCount, reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchainImages.data()));

    glGenFramebuffers(1, &renderFramebuffer.framebuffer);
    glGenRenderbuffers(1, &renderFramebuffer.depthbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, renderFramebuffer.depthbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, swapchainInfo.width, swapchainInfo.height);

    LOGI("OpenXR initialized successfully");
    return true;
}

void renderFrameVR() {
    if (!sessionRunning) return;

    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frameState{XR_TYPE_FRAME_STATE};
    xrWaitFrame(session, &waitInfo, &frameState);

    xrBeginFrame(session, nullptr);

    std::vector<XrCompositionLayerBaseHeader*> layers;
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};

    if (frameState.shouldRender) {
        uint32_t imageIndex;
        xrAcquireSwapchainImage(swapchain, nullptr, &imageIndex);

        XrSwapchainImageWaitInfo waitImageInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, nullptr, XR_INFINITE_DURATION};
        xrWaitSwapchainImage(swapchain, &waitImageInfo);

        XrViewState viewState{XR_TYPE_VIEW_STATE};
        XrViewLocateInfo viewLocateInfo{XR_TYPE_VIEW_LOCATE_INFO, nullptr, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, frameState.predictedDisplayTime, appSpace};
        uint32_t viewCountOutput;
        xrLocateViews(session, &viewLocateInfo, &viewState, views.size(), &viewCountOutput, views.data());

        glBindFramebuffer(GL_FRAMEBUFFER, renderFramebuffer.framebuffer);

        for (uint32_t eye = 0; eye < viewCountOutput; ++eye) {
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, swapchainImages[imageIndex].khr.image, 0, eye);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, renderFramebuffer.depthbuffer);

            const auto& vp = viewConfigViews[eye];
            glViewport(0, 0, vp.recommendedImageRectWidth, vp.recommendedImageRectHeight);

            glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            float projMatrix[16];
            float viewMatrix[16];
            matrix_create_projection_from_fov(views[eye].fov, 0.1f, 100.0f, projMatrix);
            matrix_create_view_from_pose(views[eye].pose, viewMatrix);

            float viewProjMatrix[16];
            matrix_multiply(projMatrix, viewMatrix, viewProjMatrix);

            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glUseProgram(shaderProgram);
            float modelMatrix[16], mvp[16];
            matrix_translate(0.0f, 0.0f, -3.0f, modelMatrix);
            matrix_multiply(viewProjMatrix, modelMatrix, mvp);
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "mvp"), 1, GL_FALSE, mvp);
            glUniform3f(glGetUniformLocation(shaderProgram, "color"), 0.2f, 0.3f, 0.8f);
            glBindVertexArray(VAO);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            glUseProgram(overlayShaderProgram);

            matrix_translate(0.3f, 0.2f, -1.5f, modelMatrix);
            matrix_multiply(viewProjMatrix, modelMatrix, mvp);
            glUniformMatrix4fv(glGetUniformLocation(overlayShaderProgram, "mvp"), 1, GL_FALSE, mvp);
            glUniform3f(glGetUniformLocation(overlayShaderProgram, "color"), 1.0f, 0.2f, 0.2f);
            glUniform1f(glGetUniformLocation(overlayShaderProgram, "alpha"), 0.7f);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

            matrix_translate(-0.3f, -0.2f, -2.0f, modelMatrix);
            matrix_multiply(viewProjMatrix, modelMatrix, mvp);
            glUniformMatrix4fv(glGetUniformLocation(overlayShaderProgram, "mvp"), 1, GL_FALSE, mvp);
            glUniform3f(glGetUniformLocation(overlayShaderProgram, "color"), 0.2f, 1.0f, 0.2f);
            glUniform1f(glGetUniformLocation(overlayShaderProgram, "alpha"), 0.6f);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glDisable(GL_DEPTH_TEST);

            projectionViews[eye] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
            projectionViews[eye].pose = views[eye].pose;
            projectionViews[eye].fov = views[eye].fov;
            projectionViews[eye].subImage.swapchain = swapchain;
            projectionViews[eye].subImage.imageRect.offset = {0, 0};
            projectionViews[eye].subImage.imageRect.extent = {(int32_t)vp.recommendedImageRectWidth, (int32_t)vp.recommendedImageRectHeight};
            projectionViews[eye].subImage.imageArrayIndex = eye;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        xrReleaseSwapchainImage(swapchain, nullptr);

        layer.space = appSpace;
        layer.viewCount = viewCountOutput;
        layer.views = projectionViews.data();
        layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer));
    }

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = layers.size();
    endInfo.layers = layers.data();
    xrEndFrame(session, &endInfo);
}

void pollEvents() {
    if (instance == XR_NULL_HANDLE) return;
    XrEventDataBuffer eventData{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(instance, &eventData) == XR_SUCCESS) {
        switch (eventData.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                auto* stateEvent = reinterpret_cast<XrEventDataSessionStateChanged*>(&eventData);
                sessionState = stateEvent->state;
                LOGI("Session state changed to %d", sessionState);
                if (sessionState == XR_SESSION_STATE_READY) {
                    XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO, nullptr, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
                    if (XR_SUCCEEDED(xrBeginSession(session, &beginInfo))) {
                        sessionRunning = true;
                        LOGI("Session started successfully");
                    }
                } else if (sessionState == XR_SESSION_STATE_STOPPING) {
                    xrEndSession(session);
                    sessionRunning = false;
                }
                break;
            }
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                LOGI("Instance loss pending. Exiting.");
                sessionRunning = false;
                break;
            default: break;
        }
        eventData = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}
#endif

// --- MOBILE-ONLY RENDER FUNCTION ---
#if defined(TEST_ON_MOBILE)
void renderFrameMobile() {
    if (eglDisplay == EGL_NO_DISPLAY) {
        return;
    }

    glViewport(0, 0, windowWidth, windowHeight);
    // Set background to cyan as per the problem statement's resulting image
    glClearColor(0.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(overlayShaderProgram);
    glBindVertexArray(VAO);

    float translateMatrix[16];
    float scaleMatrix[16];
    float mvp[16];

    // --- Blue Quad (Top-Left) ---
    matrix_translate(-0.4f, 0.4f, 0.0f, translateMatrix);
    matrix_scale(0.5f, 0.5f, 1.0f, scaleMatrix);
    matrix_multiply(translateMatrix, scaleMatrix, mvp);
    glUniformMatrix4fv(glGetUniformLocation(overlayShaderProgram, "mvp"), 1, GL_FALSE, mvp);
    glUniform3f(glGetUniformLocation(overlayShaderProgram, "color"), 0.0f, 0.0f, 1.0f); // Blue
    glUniform1f(glGetUniformLocation(overlayShaderProgram, "alpha"), 1.0f);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    // --- Magenta Quad (Middle) ---
    matrix_translate(0.0f, 0.0f, 0.0f, translateMatrix);
    matrix_scale(0.5f, 0.5f, 1.0f, scaleMatrix);
    matrix_multiply(translateMatrix, scaleMatrix, mvp);
    glUniformMatrix4fv(glGetUniformLocation(overlayShaderProgram, "mvp"), 1, GL_FALSE, mvp);
    glUniform3f(glGetUniformLocation(overlayShaderProgram, "color"), 1.0f, 0.0f, 1.0f); // Magenta
    glUniform1f(glGetUniformLocation(overlayShaderProgram, "alpha"), 1.0f);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    // --- Green Quad (Right) ---
    matrix_translate(0.4f, 0.0f, 0.0f, translateMatrix);
    matrix_scale(0.5f, 0.5f, 1.0f, scaleMatrix);
    matrix_multiply(translateMatrix, scaleMatrix, mvp);
    glUniformMatrix4fv(glGetUniformLocation(overlayShaderProgram, "mvp"), 1, GL_FALSE, mvp);
    glUniform3f(glGetUniformLocation(overlayShaderProgram, "color"), 0.0f, 1.0f, 0.0f); // Green
    glUniform1f(glGetUniformLocation(overlayShaderProgram, "alpha"), 1.0f);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);


    glDisable(GL_BLEND);

    eglSwapBuffers(eglDisplay, eglSurface);
}
#endif

// --- Main App Logic ---

void handleAppCmd(android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window != nullptr) {
                initEGL(app);
                initOpenGL();
#if !defined(TEST_ON_MOBILE)
                initOpenXR(app);
#endif
            }
            break;
        case APP_CMD_TERM_WINDOW:
            cleanup();
            break;
    }
}

int32_t handle_input(struct android_app* app, AInputEvent* event) {
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event);
        if (action == AMOTION_EVENT_ACTION_DOWN) {
            float x = AMotionEvent_getX(event, 0);
            float y = AMotionEvent_getY(event, 0);

            // Convert screen pixel coordinates to Normalized Device Coordinates (NDC) [-1, 1]
            float ndc_x = (x / (float)windowWidth) * 2.0f - 1.0f;
            float ndc_y = -((y / (float)windowHeight) * 2.0f - 1.0f); // Y is inverted in OpenGL

            // Define bounding boxes for each quad in NDC
            // BBox: {minX, maxX, minY, maxY}
            float blue_bbox[]   = {-0.65f, -0.15f, 0.15f, 0.65f};
            float magenta_bbox[]= {-0.25f, 0.25f, -0.25f, 0.25f};
            float green_bbox[]  = {0.15f, 0.65f, -0.25f, 0.25f};

            if (ndc_x > blue_bbox[0] && ndc_x < blue_bbox[1] && ndc_y > blue_bbox[2] && ndc_y < blue_bbox[3]) {
                LOGI("Blue Quad Tapped!");
            } else if (ndc_x > magenta_bbox[0] && ndc_x < magenta_bbox[1] && ndc_y > magenta_bbox[2] && ndc_y < magenta_bbox[3]) {
                LOGI("Magenta Quad Tapped!");
            } else if (ndc_x > green_bbox[0] && ndc_x < green_bbox[1] && ndc_y > green_bbox[2] && ndc_y < green_bbox[3]) {
                LOGI("Green Quad Tapped!");
            } else {
                LOGI("Background Tapped.");
            }
        }
        return 1;
    }
    return 0;
}

void android_main(android_app* app) {
    app->onAppCmd = handleAppCmd;
    app->onInputEvent = handle_input;

#if !defined(TEST_ON_MOBILE)
    // Initialize OpenXR loader for VR mode
    PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR = nullptr;
    xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&xrInitializeLoaderKHR);
    if (xrInitializeLoaderKHR == nullptr) {
        LOGE("Failed to get xrInitializeLoaderKHR function pointer!");
        return;
    }

    XrLoaderInitInfoAndroidKHR loaderInitInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    loaderInitInfo.applicationVM = app->activity->vm;
    loaderInitInfo.applicationContext = app->activity->clazz;
    if (XR_FAILED(xrInitializeLoaderKHR( (const XrLoaderInitInfoBaseHeaderKHR*)&loaderInitInfo ))) {
        LOGE("Failed to initialize OpenXR loader!");
        return;
    }
    LOGI("OpenXR Loader Initialized Successfully.");
#endif

    while (true) {
        int events;
        android_poll_source* source;
        int timeoutMs = 0; // Always poll for events

#if !defined(TEST_ON_MOBILE)
        // For VR, block if the session isn't running
        timeoutMs = !sessionRunning && app->destroyRequested == 0 ? -1 : 0;
#endif

        if (ALooper_pollOnce(timeoutMs, nullptr, &events, (void**)&source) >= 0) {
            if (source) {
                source->process(app, source);
            }
        }

        if (app->destroyRequested) {
            cleanup();
            return;
        }

#if defined(TEST_ON_MOBILE)
        if (eglDisplay != EGL_NO_DISPLAY) {
            renderFrameMobile();
        }
#else
        pollEvents();
        if (sessionRunning) {
            renderFrameVR();
        }
#endif
    }
}


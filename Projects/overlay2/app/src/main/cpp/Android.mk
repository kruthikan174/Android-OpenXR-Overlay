LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := openxr_overlay_app
LOCAL_SRC_FILES := main.cpp
LOCAL_CPPFLAGS := -std=c++17 -fexceptions -frtti
LOCAL_CFLAGS := -DANDROID -DXR_USE_PLATFORM_ANDROID
LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv3

# OpenXR
LOCAL_C_INCLUDES += $(LOCAL_PATH)/openxr/include
LOCAL_LDLIBS += -L$(LOCAL_PATH)/openxr/libs/arm64-v8a -lopenxr_loader

# Android Native App Glue
LOCAL_STATIC_LIBRARIES := android_native_app_glue

include $(BUILD_SHARED_LIBRARY)

$(call import-module,android/native_app_glue)
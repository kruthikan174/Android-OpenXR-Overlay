cmake_minimum_required(VERSION 3.10.2)

set(CMAKE_SYSTEM_VERSION ${ANDROID_PLATFORM})

project("overlay_app_cpp")

# STEP 1: Define the library and ALL its source files.
# We add android_native_app_glue.c directly from the NDK source.
add_library(
        overlay_app_cpp
        SHARED
        custom_monado_runtime.cpp
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)

# STEP 2: Tell the library where to find the headers.
target_include_directories(overlay_app_cpp PRIVATE
        ${CMAKE_SOURCE_DIR}/openxr/include
        ${ANDROID_NDK}/sources/android/native_app_glue
)

# Find the other required Android libraries
find_library(log-lib log)
find_library(android-lib android)
find_library(egl-lib EGL)
find_library(glesv3-lib GLESv3)

# Add the prebuilt openxr_loader.so library
add_library(openxr_loader SHARED IMPORTED)
set_target_properties(openxr_loader PROPERTIES IMPORTED_LOCATION
        ${CMAKE_SOURCE_DIR}/openxr/libs/${CMAKE_ANDROID_ARCH_ABI}/libopenxr_loader.so)

# Link all the libraries together
# Note that native_app_glue_lib is gone because we compiled it directly.
target_link_libraries(
        overlay_app_cpp
        ${log-lib}
        ${android-lib}
        ${egl-lib}
        ${glesv3-lib}
        openxr_loader
        c++_shared
)

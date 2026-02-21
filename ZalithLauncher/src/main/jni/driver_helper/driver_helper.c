//
// Created by Vera-Firefly on 17.01.2025.
// Modified to load SwiftShader Vulkan driver (libvk_swiftshader.so)
//
#include <EGL/egl.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <android/dlext.h>
#include "nsbypass.h"
#include "GL/gl.h"

// Always return true for SwiftShader (software rendering)
bool checkAdrenoGraphics() {
    return true;
}

void* loadTurnipVulkan() {
    if (!checkAdrenoGraphics())
        return NULL;

    const char* native_dir = getenv("DRIVER_PATH");
    const char* cache_dir = getenv("TMPDIR");

    if (!native_dir) 
        return NULL;

    if (!linker_ns_load(native_dir))
        return NULL;

    void* linkerhook = linker_ns_dlopen("liblinkerhook.so", RTLD_LOCAL | RTLD_NOW);
    if (!linkerhook)
        return NULL;

    // Load SwiftShader driver instead of Turnip
    void* swiftshader_driver_handle = linker_ns_dlopen("libvk_swiftshader.so", RTLD_LOCAL | RTLD_NOW);
    if (!swiftshader_driver_handle) {
        dlclose(linkerhook);
        return NULL;
    }

    void* dl_android = linker_ns_dlopen("libdl_android.so", RTLD_LOCAL | RTLD_LAZY);
    if (!dl_android) {
        dlclose(linkerhook);
        dlclose(swiftshader_driver_handle);
        return NULL;
    }

    void* android_get_exported_namespace = dlsym(dl_android, "android_get_exported_namespace");
    void (*linkerhookPassHandles)(void*, void*, void*) = dlsym(linkerhook, "linker_hook_set_handles");

    if (!linkerhookPassHandles || !android_get_exported_namespace) {
        dlclose(dl_android);
        dlclose(linkerhook);
        dlclose(swiftshader_driver_handle);
        return NULL;
    }

    // Pass the SwiftShader handle to the hook
    linkerhookPassHandles(swiftshader_driver_handle, android_dlopen_ext, android_get_exported_namespace);

    void* libvulkan = linker_ns_dlopen_unique(cache_dir, "libvulkan.so", RTLD_LOCAL | RTLD_NOW);
    if (!libvulkan) {
        dlclose(dl_android);
        dlclose(linkerhook);
        dlclose(swiftshader_driver_handle);
        return NULL;
    }

    return libvulkan;
}

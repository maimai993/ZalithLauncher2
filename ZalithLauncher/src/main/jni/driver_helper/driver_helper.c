//
// Created by Vera-Firefly on 17.01.2025.
//
#include <EGL/egl.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <android/dlext.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include "nsbypass.h"
#include "GL/gl.h"

//#define ADRENO_POSSIBLE
#ifdef ADRENO_POSSIBLE

bool checkAdrenoGraphics() {
    EGLDisplay eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglDisplay == EGL_NO_DISPLAY || eglInitialize(eglDisplay, NULL, NULL) != EGL_TRUE) 
        return false;

    EGLint egl_attributes[] = {
        EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE
    };

    EGLint num_configs = 0;
    if (eglChooseConfig(eglDisplay, egl_attributes, NULL, 0, &num_configs) != EGL_TRUE || num_configs == 0) {
        eglTerminate(eglDisplay);
        return false;
    }

    EGLConfig eglConfig;
    eglChooseConfig(eglDisplay, egl_attributes, &eglConfig, 1, &num_configs);

    const EGLint egl_context_attributes[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext context = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, egl_context_attributes);
    if (context == EGL_NO_CONTEXT) {
        eglTerminate(eglDisplay);
        return false;
    }

    if (eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, context) != EGL_TRUE) {
        eglDestroyContext(eglDisplay, context);
        eglTerminate(eglDisplay);
        return false;
    }

    const char* vendor = (const char*)glGetString(GL_VENDOR);
    const char* renderer = (const char*)glGetString(GL_RENDERER);

    bool is_adreno = (vendor && renderer && strcmp(vendor, "Qualcomm") == 0 && strstr(renderer, "Adreno") != NULL);

    printf("GPU检测 - Vendor: %s, Renderer: %s, 是否为Adreno: %s\n", 
         vendor ? vendor : "null", 
         renderer ? renderer : "null",
         is_adreno ? "是" : "否");

    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(eglDisplay, context);
    eglTerminate(eglDisplay);

    return is_adreno;
}

void* loadTurnipVulkan() {
    /*if (!checkAdrenoGraphics())
        return NULL;*/

    printf("\n========== 驱动加载调试开始 ==========\n");
    printf("PID: %d\n", getpid());

    const char* native_dir = getenv("DRIVER_PATH");
    const char* cache_dir = getenv("TMPDIR");

    printf("环境变量:\n");
    printf("  DRIVER_PATH: %s\n", native_dir ? native_dir : "null");
    printf("  TMPDIR: %s\n", cache_dir ? cache_dir : "null");

    if (!native_dir) { 
        printf("错误: native_dir 为空!\n");
        goto commonload;
    }

    printf("尝试 linker_ns_load: %s\n", native_dir);
    if (!linker_ns_load(native_dir)) {
        printf("错误: linker_ns_load 失败, 路径: %s\n", native_dir);
        goto commonload;
    }
    printf("linker_ns_load 成功\n");

    printf("尝试加载 liblinkerhook.so...\n");
    void* linkerhook = linker_ns_dlopen("liblinkerhook.so", RTLD_LOCAL | RTLD_NOW);
    if (!linkerhook) {
        printf("错误: 无法加载 liblinkerhook.so\n");
        printf("  查找路径: %s/liblinkerhook.so\n", native_dir);
        
        // 检查文件是否存在
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/liblinkerhook.so", native_dir);
        
        FILE* file = fopen(full_path, "r");
        if (file) {
            printf("  文件存在: %s\n", full_path);
            fclose(file);
        } else {
            printf("  文件不存在: %s (errno: %d - %s)\n", full_path, errno, strerror(errno));
        }
        
        const char* dlerror_msg = dlerror();
        printf("  dlerror: %s\n", dlerror_msg ? dlerror_msg : "unknown error");
        return nullptr;
    }
    printf("liblinkerhook.so 加载成功: %p\n", linkerhook);

    const char* driverEnv = getenv("ZALTITH_DRIVER");
    const char* forceCustom = getenv("ZALITH_FORCE_CUSTOM_DRIVER");
    
    printf("\n驱动选择:\n");
    printf("  ZALTITH_DRIVER: %s\n", driverEnv ? driverEnv : "null");
    printf("  ZALITH_FORCE_CUSTOM_DRIVER: %s\n", forceCustom ? forceCustom : "null");

    void* turnip_driver_handle = nullptr;
    const char* driver_to_load = nullptr;

    if (!driverEnv && (!forceCustom || !(checkAdrenoGraphics()))) {
        driver_to_load = "libvulkan_freedreno.so";
        printf("选择默认驱动: %s\n", driver_to_load);
        turnip_driver_handle = linker_ns_dlopen(driver_to_load, RTLD_LOCAL | RTLD_NOW);
    } else {
        driver_to_load = driverEnv;
        printf("选择自定义驱动: %s\n", driver_to_load);
        turnip_driver_handle = linker_ns_dlopen(driverEnv, RTLD_LOCAL | RTLD_NOW);
    }

    if (!turnip_driver_handle) {
        printf("\n错误: 驱动加载失败!\n");
        printf("  尝试加载: %s\n", driver_to_load ? driver_to_load : "null");
        printf("  查找路径: %s/%s\n", native_dir, driver_to_load ? driver_to_load : "unknown");
        
        // 检查文件是否存在
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", native_dir, driver_to_load ? driver_to_load : "unknown");

        // 列出目录内容
        printf("\n目录 %s 内容:\n", native_dir);
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "ls -la %s/ 2>&1", native_dir);
        FILE* ls_output = popen(cmd, "r");
        if (ls_output) {
            char buffer[256];
            while (fgets(buffer, sizeof(buffer), ls_output) != NULL) {
                printf("  %s", buffer);
            }
            pclose(ls_output);
        }
        
        const char* dlerror_msg = dlerror();
        printf("  dlerror: %s\n", dlerror_msg ? dlerror_msg : "unknown error");
        
        dlclose(linkerhook);
        goto commonload;
    }
    printf("\n驱动加载成功: %s, 句柄: %p\n", driver_to_load, turnip_driver_handle);

    printf("\n尝试加载 libdl_android.so...\n");
    void* dl_android = linker_ns_dlopen("libdl_android.so", RTLD_LOCAL | RTLD_LAZY);
    if (!dl_android) {
        printf("错误: 无法加载 libdl_android.so\n");
        printf("  查找路径: %s/libdl_android.so\n", native_dir);
        
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/libdl_android.so", native_dir);
        
        const char* dlerror_msg = dlerror();
        printf("  dlerror: %s\n", dlerror_msg ? dlerror_msg : "unknown error");
        
        dlclose(linkerhook);
        dlclose(turnip_driver_handle);
        goto commonload;
    }
    printf("libdl_android.so 加载成功: %p\n", dl_android);

    printf("\n获取符号地址...\n");
    void* android_get_exported_namespace = dlsym(dl_android, "android_get_exported_namespace");
    void (*linkerhookPassHandles)(void*, void*, void*) = dlsym(linkerhook, "linker_hook_set_handles");

    if (!linkerhookPassHandles) {
        printf("错误: 无法获取 linker_hook_set_handles 符号\n");
        const char* dlerror_msg = dlerror();
        printf("  dlerror: %s\n", dlerror_msg ? dlerror_msg : "unknown");
    } else {
        printf("  linker_hook_set_handles: %p\n", linkerhookPassHandles);
    }
    
    if (!android_get_exported_namespace) {
        printf("错误: 无法获取 android_get_exported_namespace 符号\n");
        const char* dlerror_msg = dlerror();
        printf("  dlerror: %s\n", dlerror_msg ? dlerror_msg : "unknown");
    } else {
        printf("  android_get_exported_namespace: %p\n", android_get_exported_namespace);
    }

    if (!linkerhookPassHandles || !android_get_exported_namespace) {
        printf("错误: 符号获取失败\n");
        dlclose(dl_android);
        dlclose(linkerhook);
        dlclose(turnip_driver_handle);
        goto commonload;
    }

    printf("\n调用 linkerhookPassHandles...\n");
    linkerhookPassHandles(turnip_driver_handle, android_dlopen_ext, android_get_exported_namespace);
    printf("调用完成\n");

    printf("\n尝试加载 Vulkan 库...\n");
    void* libvulkan = nullptr;
    printf("尝试: linker_ns_dlopen_unique(%s, libvulkan.so)\n", cache_dir ? cache_dir : "null");
    libvulkan = linker_ns_dlopen_unique(cache_dir, "libvulkan.so", RTLD_LOCAL | RTLD_NOW);
    
    if (!libvulkan) {
        printf("警告: libvulkan.so 加载失败\n");
        
        // 检查文件是否存在
        char vulkan_path[512];
        snprintf(vulkan_path, sizeof(vulkan_path), "%s/libvulkan.so", cache_dir ? cache_dir : "/data/local/tmp");

        const char* dlerror_msg = dlerror();
        printf("  dlerror: %s\n", dlerror_msg ? dlerror_msg : "unknown");

        if (!checkAdrenoGraphics()) {
            printf("\n尝试加载 Mali 驱动: libGLES_mali.so\n");
            libvulkan = linker_ns_dlopen_unique(cache_dir, "libGLES_mali.so", RTLD_LOCAL | RTLD_NOW);
            
            if (!libvulkan) {
                printf("错误: libGLES_mali.so 加载失败\n");
                
                snprintf(vulkan_path, sizeof(vulkan_path), "%s/libGLES_mali.so", cache_dir ? cache_dir : "/data/local/tmp");
                
                dlerror_msg = dlerror();
                printf("  dlerror: %s\n", dlerror_msg ? dlerror_msg : "unknown");
            } else {
                printf("libGLES_mali.so 加载成功: %p\n", libvulkan);
            }
        }
        
        if (!libvulkan) {
            dlclose(dl_android);
            dlclose(linkerhook);
            dlclose(turnip_driver_handle);
            goto commonload;
        }
    } else {
        printf("libvulkan.so 加载成功: %p\n", libvulkan);
    }
    
    printf("\n========== 驱动加载成功 ==========\n");
    return turnip_driver_handle;

commonload:
    libvulkan = dlopen(driverEnv, RTLD_LAZY | RTLD_LOCAL);
    if (libvulkan) return libvulkan;
    if(!libvulkan) return nullptr;
}

#endif

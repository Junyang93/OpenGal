/*
 * OpenGal Android 平台层 (NativeActivity, 无 Java 代码)
 *
 * 事件流: android_main -> android_native_app_glue
 *   APP_CMD_INIT_WINDOW: 创建引擎 + EGL(渲染器内部) + 加载剧本
 *   触摸(UP):            ogal_on_tap
 * 资源: assets/ 目录通过 AAssetManager 读取(路径与剧本内相对路径一致)
 */
#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include <android/native_window.h>

#include "opengal.h"

#include <cstring>
#include <cstdlib>
#include <ctime>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "OpenGal", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "OpenGal", __VA_ARGS__)

namespace {

struct AppState {
    android_app* app = nullptr;
    ogal_engine* eng = nullptr;
    bool window_ready = false;
};

AppState g_state;

/* ---------- 平台 vtable ---------- */

void* pf_read_file(void* ud, const char* path, size_t* out_size) {
    AppState* st = (AppState*)ud;
    AAssetManager* am = st->app->activity->assetManager;
    AAsset* a = AAssetManager_open(am, path, AASSET_MODE_BUFFER);
    if (!a) return nullptr;
    size_t len = (size_t)AAsset_getLength(a);
    void* buf = malloc(len ? len : 1);
    if (!buf) { AAsset_close(a); return nullptr; }
    if (AAsset_read(a, buf, len) != (ssize_t)len) {
        free(buf); AAsset_close(a); return nullptr;
    }
    AAsset_close(a);
    *out_size = len;
    return buf;
}

void pf_log(void*, int level, const char* msg) {
    if (level == 2) LOGE("%s", msg);
    else LOGI("%s", msg);
}

uint64_t pf_time_ms(void*) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* ---------- 生命周期 ---------- */

void engine_init_window(AppState* st) {
    if (st->eng) return;
    ogal_platform pf = {};
    pf.ud = st;
    pf.read_file = pf_read_file;
    pf.log = pf_log;
    pf.time_ms = pf_time_ms;
    st->eng = ogal_create(&pf, "gl", "opensl");
    if (!st->eng) { LOGE("引擎创建失败"); return; }

    ogal_renderer* r = ogal_engine_get_renderer(st->eng);
    if (r->init(r, st->app->window) != 0) {
        LOGE("EGL 初始化失败");
        return;
    }
    int w = ANativeWindow_getWidth(st->app->window);
    int h = ANativeWindow_getHeight(st->app->window);
    ogal_resize(st->eng, w, h);
    if (ogal_load(st->eng, "script/main.gal") != 0) LOGE("剧本加载失败");
    st->window_ready = true;
}

void engine_term_window(AppState* st) {
    if (st->eng) {
        ogal_renderer* r = ogal_engine_get_renderer(st->eng);
        if (r && r->shutdown) r->shutdown(r);
        ogal_destroy(st->eng);
        st->eng = nullptr;
    }
    st->window_ready = false;
}

void on_app_cmd(android_app* app, int32_t cmd) {
    AppState* st = (AppState*)app->userData;
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        engine_init_window(st);
        break;
    case APP_CMD_TERM_WINDOW:
        engine_term_window(st);
        break;
    case APP_CMD_WINDOW_RESIZED:
        if (st->eng)
            ogal_resize(st->eng, ANativeWindow_getWidth(app->window),
                        ANativeWindow_getHeight(app->window));
        break;
    default:
        break;
    }
}

int32_t on_input(android_app* app, AInputEvent* ev) {
    AppState* st = (AppState*)app->userData;
    if (!st->eng) return 0;
    if (AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_MOTION &&
        AMotionEvent_getAction(ev) == AMOTION_EVENT_ACTION_UP) {
        float x = AMotionEvent_getX(ev, 0);
        float y = AMotionEvent_getY(ev, 0);
        ogal_on_tap(st->eng, x, y);
        return 1;
    }
    return 0;
}

} // namespace

void android_main(android_app* app) {
    app->userData = &g_state;
    g_state.app = app;
    app->onAppCmd = on_app_cmd;
    app->onInputEvent = on_input;

    ogal_event ev;
    ogal_event_type t = OGAL_EVT_NONE;
    bool stepped = false;

    for (;;) {
        int ident;
        android_poll_source* src;
        /* 等待输入时阻塞(-1), 动画/等待中立即返回(0) */
        int timeout = (g_state.window_ready && t != OGAL_EVT_NONE) ? -1 : 0;
        while ((ident = ALooper_pollOnce(timeout, nullptr, nullptr, (void**)&src)) >= 0) {
            if (src) src->process(app, src);
            timeout = 0;
            if (app->destroyRequested) {
                engine_term_window(&g_state);
                return;
            }
        }

        if (!g_state.window_ready || !g_state.eng) continue;

        if (!stepped || t == OGAL_EVT_NONE) {
            t = ogal_step(g_state.eng, &ev);
            stepped = true;
        }
        ogal_render(g_state.eng);
    }
}

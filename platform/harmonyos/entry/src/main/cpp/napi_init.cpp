/*
 * OpenGal HarmonyOS 平台层 (XComponent + NAPI)
 *
 * 流程:
 *   ArkTS Index.ets onLoad -> 调用 native 导出的 register(xcomponentContext, resourceManager)
 *   -> OH_NativeXComponent_RegisterCallback 注册 surface 回调
 *   -> OnSurfaceCreated: 创建引擎, EGL(渲染器内部, window 为 OHNativeWindow*)
 *   -> 独立渲染线程: step/render 循环
 *   -> DispatchTouchEvent: 触摸转发 ogal_on_tap
 *
 * 资源: 游戏目录拷贝到 entry/src/main/resources/rawfile/ 下,
 *       通过 NativeResourceManager 按相对路径读取(tools/pack.py 可完成拷贝)。
 */
#include "napi/native_api.h"
#include <ace/xcomponent/native_interface_xcomponent.h>
#include <hilog/log.h>
#include <rawfile/raw_file.h>
#include <rawfile/raw_file_manager.h>

#include "opengal.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <thread>

#define LOG_TAG "OpenGal"
#define LOGI(...) OH_LOG_INFO(LOG_APP, __VA_ARGS__)
#define LOGE(...) OH_LOG_ERROR(LOG_APP, __VA_ARGS__)

namespace {

OH_NativeXComponent* g_xcomp = nullptr;
NativeResourceManager* g_resMgr = nullptr;
ogal_engine* g_eng = nullptr;
std::mutex g_mtx;
std::thread g_loop;
std::atomic<bool> g_running{false};

/* ---------- 平台 vtable ---------- */

void* pf_read_file(void*, const char* path, size_t* out_size) {
    if (!g_resMgr) return nullptr;
    RawFile* rf = OH_ResourceManager_OpenRawFile(g_resMgr, path);
    if (!rf) return nullptr;
    long size = OH_ResourceManager_GetRawFileSize(rf);
    if (size < 0) { OH_ResourceManager_CloseRawFile(rf); return nullptr; }
    void* buf = malloc(size > 0 ? (size_t)size : 1);
    if (!buf) { OH_ResourceManager_CloseRawFile(rf); return nullptr; }
    int rd = OH_ResourceManager_ReadRawFile(rf, buf, (int)size);
    OH_ResourceManager_CloseRawFile(rf);
    if (rd != (int)size) { free(buf); return nullptr; }
    *out_size = (size_t)size;
    return buf;
}

void pf_log(void*, int level, const char* msg) {
    if (level == 2) LOGE("%{public}s", msg);
    else LOGI("%{public}s", msg);
}

uint64_t pf_time_ms(void*) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* ---------- 渲染线程 ---------- */

void render_loop() {
    ogal_event ev;
    ogal_event_type t = OGAL_EVT_NONE;
    bool stepped = false;
    while (g_running) {
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            if (!g_eng) break;
            if (!stepped || t == OGAL_EVT_NONE) {
                t = ogal_step(g_eng, &ev);
                stepped = true;
            }
            ogal_render(g_eng);
        }
        if (t == OGAL_EVT_NONE) std::this_thread::sleep_for(std::chrono::milliseconds(16));
        else std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
}

/* ---------- XComponent 回调 ---------- */

void OnSurfaceCreated(OH_NativeXComponent* xc, void* window) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_eng) return;

    ogal_platform pf = {};
    pf.ud = nullptr;
    pf.read_file = pf_read_file;
    pf.log = pf_log;
    pf.time_ms = pf_time_ms;
    g_eng = ogal_create(&pf, "gl", "opensl");
    if (!g_eng) { LOGE("引擎创建失败"); return; }

    ogal_renderer* r = ogal_engine_get_renderer(g_eng);
    if (r->init(r, window) != 0) { LOGE("EGL 初始化失败"); return; }

    char id[128] = {0};
    uint64_t id_size = sizeof(id);
    OH_NativeXComponent_GetXComponentId(xc, id, &id_size);
    uint64_t w = 0, h = 0;
    OH_NativeXComponent_GetXComponentSize(xc, id, &w, &h);
    ogal_resize(g_eng, (int)w, (int)h);

    if (ogal_load(g_eng, "script/main.gal") != 0) LOGE("剧本加载失败");

    g_running = true;
    g_loop = std::thread(render_loop);
}

void OnSurfaceChanged(OH_NativeXComponent* xc, void* window) {
    (void)window;
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_eng) return;
    char id[128] = {0};
    uint64_t id_size = sizeof(id);
    OH_NativeXComponent_GetXComponentId(xc, id, &id_size);
    uint64_t w = 0, h = 0;
    OH_NativeXComponent_GetXComponentSize(xc, id, &w, &h);
    ogal_resize(g_eng, (int)w, (int)h);
}

void OnSurfaceDestroyed(OH_NativeXComponent*, void*) {
    g_running = false;
    if (g_loop.joinable()) g_loop.join();
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_eng) { ogal_destroy(g_eng); g_eng = nullptr; }
}

void OnDispatchTouchEvent(OH_NativeXComponent* xc, void* window) {
    OH_NativeXComponent_TouchEvent te;
    if (OH_NativeXComponent_GetTouchEvent(xc, window, &te) !=
        OH_NATIVEXCOMPONENT_RESULT_SUCCESS) return;
    /* actionType: API 12+ 头文件提供; 更早版本请改用 OH_NativeXComponent_GetTouchInfo */
    if (te.actionType == OH_NATIVEXCOMPONENT_UP) {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (g_eng) ogal_on_tap(g_eng, te.x, te.y);
    }
}

OH_NativeXComponent_Callback g_callback = {
    .OnSurfaceCreated = OnSurfaceCreated,
    .OnSurfaceChanged = OnSurfaceChanged,
    .OnSurfaceDestroyed = OnSurfaceDestroyed,
    .DispatchTouchEvent = OnDispatchTouchEvent,
};

/* ---------- NAPI 导出 ---------- */

napi_value Register(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr, nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) return nullptr;
    napi_unwrap(env, args[0], (void**)&g_xcomp);
    g_resMgr = OH_ResourceManager_InitNativeResourceManager(env, args[1]);
    if (!g_xcomp || !g_resMgr) { LOGE("register 参数无效"); return nullptr; }
    OH_NativeXComponent_RegisterCallback(g_xcomp, &g_callback);
    LOGI("OpenGal HarmonyOS 平台层已注册");
    return nullptr;
}

} // namespace

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {
        {"register", nullptr, Register, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, 1, desc);
    return exports;
}
EXTERN_C_END

static napi_module g_module = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "entry",
    .nm_priv = nullptr,
    .reserved = {0},
};

extern "C" __attribute__((constructor)) void RegisterEntryModule() {
    napi_module_register(&g_module);
}

/*
 * OpenGal Windows 平台层
 *
 * 职责: 窗口 / WGL 上下文(由渲染器内部创建) / 文件读取 / 消息循环 / 点击转发。
 * 用法: galdemo [游戏目录]   默认 games/demo
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "opengal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static ogal_engine* g_eng = nullptr;

/* ---------- 平台 vtable ---------- */

struct PfUd {
    std::string root; /* 游戏资源根目录, 以 '/' 结尾 */
};

static void* pf_read_file(void* ud, const char* path, size_t* out_size) {
    PfUd* p = (PfUd*)ud;
    std::string full = p->root + path;
    FILE* f = nullptr;
    fopen_s(&f, full.c_str(), "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) { fclose(f); return nullptr; }
    void* buf = malloc((size_t)len);
    if (!buf) { fclose(f); return nullptr; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf); fclose(f); return nullptr;
    }
    fclose(f);
    *out_size = (size_t)len;
    return buf;
}

static void pf_log(void*, int level, const char* msg) {
    const char* tag = level == 2 ? "ERR " : (level == 1 ? "WARN" : "INFO");
    char buf[1024];
    snprintf(buf, sizeof(buf), "[OpenGal %s] %s\n", tag, msg);
    OutputDebugStringA(buf);
    printf("%s", buf);
}

static uint64_t pf_time_ms(void*) { return (uint64_t)GetTickCount64(); }

/* ---------- 窗口 ---------- */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_LBUTTONDOWN:
        if (g_eng) ogal_on_tap(g_eng, (float)LOWORD(lp), (float)HIWORD(lp));
        return 0;
    case WM_KEYDOWN:
        if (g_eng && (wp == VK_SPACE || wp == VK_RETURN)) {
            int w = 0, h = 0;
            ogal_get_size(g_eng, &w, &h);
            ogal_on_tap(g_eng, (float)w * 0.5f, (float)h * 0.5f);
        }
        return 0;
    case WM_SIZE:
        if (g_eng && wp != SIZE_MINIMIZED)
            ogal_resize(g_eng, LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR cmd, int) {
    const char* game_dir = (cmd && cmd[0]) ? cmd : "games/demo";
    PfUd* ud = new PfUd();
    ud->root = std::string(game_dir) + "/";

    ogal_platform pf = {};
    pf.ud = ud;
    pf.read_file = pf_read_file;
    pf.log = pf_log;
    pf.time_ms = pf_time_ms;

    g_eng = ogal_create(&pf, "gl", "null");
    if (!g_eng) return 1;

    const int W = 1280, H = 720;
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "OpenGalWindow";
    RegisterClassExA(&wc);

    RECT rc = { 0, 0, W, H };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExA(0, "OpenGalWindow", "OpenGal Demo",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;

    /* GL 渲染器在 init 时基于 HWND 创建 WGL 上下文 */
    ogal_renderer* r = ogal_engine_get_renderer(g_eng);
    if (!r || r->init(r, hwnd) != 0) {
        pf_log(nullptr, 2, "GL 渲染器初始化失败");
        return 1;
    }
    ogal_resize(g_eng, W, H);

    if (ogal_load(g_eng, "script/main.gal") != 0) {
        pf_log(nullptr, 2, "剧本加载失败(检查游戏目录与资源)");
        return 1;
    }

    ogal_event ev;
    ogal_event_type t = ogal_step(g_eng, &ev);
    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running) break;

        if (t == OGAL_EVT_END) {
            ogal_render(g_eng);
            Sleep(100);
            continue;
        }
        ogal_render(g_eng);
        if (t == OGAL_EVT_NONE) {
            Sleep(16); /* wait/淡入淡出中 */
        } else {
            WaitMessage(); /* 对话/选项: 休眠等待点击唤醒 */
        }
        t = ogal_step(g_eng, &ev);
    }

    ogal_destroy(g_eng);
    delete ud;
    return 0;
}

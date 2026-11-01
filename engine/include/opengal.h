/*
 * OpenGal - 跨平台 Galgame 引擎公开 C API
 *
 * 设计目标:
 *   1. 引擎核心与平台完全解耦, 通过 vtable 注入平台能力:
 *      - ogal_platform   : 文件读取 / 日志 / 时钟 / 渲染器与音频工厂
 *      - ogal_renderer   : 纹理 / 矩形 / 文本 / 帧呈现
 *      - ogal_audio      : 音乐 / 音效
 *   2. 非阻塞步进模型, 适合 Win32 消息循环与 Android / HarmonyOS 的 UI 线程。
 *   3. 新增平台只需实现三个 vtable 即可获得完整引擎能力。
 *
 * 移植指南见 docs/porting.md
 */
#ifndef OPENGAL_H
#define OPENGAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* ================================================================
 * 渲染器 vtable
 * 坐标系: 左上角为原点, 单位像素, 由引擎完成布局。
 * ================================================================ */
typedef struct ogal_renderer {
    void* ud; /* 平台私有数据 */

    /* native_window: 平台相关的绘图表面
     *   Win32     -> HWND
     *   Android   -> ANativeWindow* (EGL 已忽略, init 时可为 NULL, 见各平台层)
     *   HarmonyOS -> OHNativeWindow*
     * 返回 0 表示成功 */
    int  (*init)(struct ogal_renderer* r, void* native_window);
    void (*shutdown)(struct ogal_renderer* r);
    void (*resize)(struct ogal_renderer* r, int w, int h);

    /* 纹理: rgba 为 RGBA8888 像素, 自下而上排列与否由平台统一(bottom-up) */
    void* (*create_texture)(struct ogal_renderer* r, const unsigned char* rgba, int w, int h);
    void  (*destroy_texture)(struct ogal_renderer* r, void* tex);
    /* 绘制纹理, (x,y) 为左上角, alpha 0~1 */
    void  (*draw_texture)(struct ogal_renderer* r, void* tex,
                          float x, float y, float w, float h, float alpha);

    void (*fill_rect)(struct ogal_renderer* r, float x, float y, float w, float h,
                      float cr, float cg, float cb, float ca);

    /* 文本: utf8 编码, px 为字号高度, (x,y) 为左上角 */
    void  (*draw_text)(struct ogal_renderer* r, const char* utf8,
                       float x, float y, float px,
                       float cr, float cg, float cb, float ca);
    float (*text_width)(struct ogal_renderer* r, const char* utf8, float px);

    /* 可选: 注入 TTF 字体数据(引擎发现 font/default.ttf 时调用), 返回 0 成功 */
    int (*set_font)(struct ogal_renderer* r, const unsigned char* ttf_data, int len);

    void (*begin)(struct ogal_renderer* r);   /* 清帧 / 置投影 */
    void (*present)(struct ogal_renderer* r); /* 交换缓冲 */
    void (*destroy)(struct ogal_renderer* r);
} ogal_renderer;

/* 引擎内置的 GL 渲染器工厂(渲染后端见 engine/render) */
ogal_renderer* ogal_create_gl_renderer(void);

/* ================================================================
 * 音频 vtable
 * data 为完整文件内容(引擎负责释放), 内置 WAV(PCM16) 解码。
 * ================================================================ */
typedef struct ogal_audio {
    void* ud;
    int  (*init)(struct ogal_audio* a);
    /* loop: BGM 循环播放 */
    void (*play_music)(struct ogal_audio* a, const unsigned char* data, int len, int loop);
    void (*play_sound)(struct ogal_audio* a, const unsigned char* data, int len);
    void (*stop_music)(struct ogal_audio* a);
    void (*destroy)(struct ogal_audio* a);
} ogal_audio;

ogal_audio* ogal_create_null_audio(void);                 /* 静音后端(Win32 默认) */
ogal_audio* ogal_create_opensl_audio(void);               /* OpenSL ES(Android / HarmonyOS) */

/* ================================================================
 * 平台 vtable
 * ================================================================ */
typedef struct ogal_platform {
    void* ud;

    /* 读取整个文件, 返回 malloc 分配的缓冲区(*out_size 为字节数),
     * 失败返回 NULL。引擎用 free() 释放。
     * path 为剧本内相对路径, 如 "bg/classroom.png"。 */
    void* (*read_file)(void* ud, const char* path, size_t* out_size);

    void (*log)(void* ud, int level, const char* msg); /* level: 0=info 1=warn 2=error */
    uint64_t (*time_ms)(void* ud);
} ogal_platform;

/* ================================================================
 * 引擎
 * ================================================================ */
typedef struct ogal_engine ogal_engine;

typedef enum {
    OGAL_EVT_NONE = 0,  /* VM 处于 wait/过渡中, 稍后(约 16ms)再次调用 ogal_step */
    OGAL_EVT_SAY,       /* 显示对话, 等待用户点击 */
    OGAL_EVT_CHOICE,    /* 显示选项, 等待用户点击选项 */
    OGAL_EVT_END        /* 剧本结束 */
} ogal_event_type;

typedef struct ogal_event {
    ogal_event_type type;
    const char* speaker;   /* 可能为 NULL(旁白) */
    const char* text;      /* 对话文本 */
    int         choice_count;
    const char* choices[16];
} ogal_event;

/* pf 的生命周期须不短于引擎。renderer / audio 由引擎创建并托管。 */
ogal_engine* ogal_create(const ogal_platform* pf, const char* renderer_hint,
                         const char* audio_hint); /* hint: "gl"/"null"/"opensl", NULL 用默认 */
void ogal_destroy(ogal_engine* e);

/* 加载剧本(如 "script/main.gal"), 并预加载其中引用的静态资源清单。
 * 返回 0 成功, 非 0 为错误码。 */
int ogal_load(ogal_engine* e, const char* script_path);

/* 推进 VM, 直到:
 *   - 遇到对话 / 选项 / 剧本结束 -> 返回对应事件
 *   - 遇到 wait / 淡入淡出进行中  -> 返回 OGAL_EVT_NONE(稍后重试)
 * 返回的事件指针仅在下次 ogal_step 之前有效。 */
ogal_event_type ogal_step(ogal_engine* e, ogal_event* out_evt);

/* 用户点击屏幕: SAY 状态推进文本; CHOICE 状态命中按钮则选中, 否则忽略。
 * (x,y) 为像素坐标。平台只需把"点击"事件原样转发进来, UI 逻辑引擎内部完成。 */
void ogal_on_tap(ogal_engine* e, float x, float y);

/* 每帧调用: 绘制背景 / 立绘 / 对话框 / 选项 */
void ogal_render(ogal_engine* e);
void ogal_resize(ogal_engine* e, int w, int h);

/* 查询舞台尺寸(平台层在创建表面后同步给引擎) */
void ogal_get_size(ogal_engine* e, int* w, int* h);

/* 平台层取出引擎内部的渲染器以完成 init(native_window 由平台创建) */
ogal_renderer* ogal_engine_get_renderer(ogal_engine* e);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OPENGAL_H */

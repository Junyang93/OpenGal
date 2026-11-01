/*
 * OpenGal 内置 GL 渲染器
 *
 * 两种编译形态:
 *   - 默认(Windows): WGL 上下文 + OpenGL 1.1 固定管线 (链接 opengl32)
 *   - OGAL_GLES2 定义时(Android / HarmonyOS): EGL 上下文 + OpenGL ES 2.0 可编程管线
 *
 * 文本: stb_truetype 按需光栅化到 1024x1024 图集, 支持中文等任意 Unicode。
 * 纹理: create_texture 传入的 RGBA 像素为自上而下, 上传时翻转为 GL 习惯的自下而上。
 */
#include "opengal.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

#ifdef OGAL_GLES2
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h" /* 构建时由 CMake 下载到 STB_DIR */

namespace {

constexpr int ATLAS_W = 1024;
constexpr int ATLAS_H = 1024;

struct Tex {
    GLuint id;
    int w, h;
};

struct Glyph {
    int x = 0, y = 0, w = 0, h = 0; /* 图集内位置(像素) */
    float xoff = 0, yoff = 0, advance = 0;
};

struct GLR {
    ogal_renderer base;

#ifdef OGAL_GLES2
    EGLDisplay dpy = nullptr;
    EGLSurface surf = nullptr;
    EGLContext ctx = nullptr;
    EGLConfig config = nullptr;
    GLuint prog = 0;
    GLint u_res = -1, u_color = -1, u_tex = -1, u_use_tex = -1;
    GLint a_pos = -1, a_uv = -1;
#else
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    HGLRC hrc = nullptr;
#endif
    int w = 0, h = 0;

    /* 字体 */
    std::vector<unsigned char> font_data;
    stbtt_fontinfo font;
    bool font_ready = false;
    GLuint atlas_tex = 0;
    int atlas_x = 1, atlas_y = 1, atlas_row_h = 0;
    std::map<unsigned long long, Glyph> glyphs; /* key = (codepoint<<20)|px */
};

/* ---------- 图元绘制 ---------- */

#ifdef OGAL_GLES2

static const char* kVS =
    "attribute vec2 aPos; attribute vec2 aUV;\n"
    "uniform vec2 uRes; varying vec2 vUV;\n"
    "void main(){ vUV = aUV;\n"
    "  gl_Position = vec4(aPos.x/uRes.x*2.0-1.0, 1.0-aPos.y/uRes.y*2.0, 0.0, 1.0); }";
static const char* kFS =
    "precision mediump float; varying vec2 vUV;\n"
    "uniform sampler2D uTex; uniform vec4 uColor; uniform float uUseTex;\n"
    "void main(){ vec4 t = (uUseTex > 0.5) ? texture2D(uTex, vUV) : vec4(1.0);\n"
    "  gl_FragColor = uColor * t; }";

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    return s;
}

static void gles_init_shaders(GLR* g) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, kVS);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kFS);
    g->prog = glCreateProgram();
    glAttachShader(g->prog, vs);
    glAttachShader(g->prog, fs);
    glLinkProgram(g->prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glUseProgram(g->prog);
    g->u_res = glGetUniformLocation(g->prog, "uRes");
    g->u_color = glGetUniformLocation(g->prog, "uColor");
    g->u_tex = glGetUniformLocation(g->prog, "uTex");
    g->u_use_tex = glGetUniformLocation(g->prog, "uUseTex");
    g->a_pos = glGetAttribLocation(g->prog, "aPos");
    g->a_uv = glGetAttribLocation(g->prog, "aUV");
    glUniform1i(g->u_tex, 0);
}

/* 以 (x,y) 为左上角绘制矩形; (u0,v0)/(u1,v1) 对应纹理左上/右下 */
static void draw_quad(GLR* g, float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1,
                      float r, float gr, float b, float a, int textured, GLuint tex) {
    float pos[8] = {
        x, y + h,  x, y,  x + w, y + h,  x + w, y
    };
    float uv[8] = {
        u0, v1,  u0, v0,  u1, v1,  u1, v0
    };
    glUseProgram(g->prog);
    glUniform2f(g->u_res, (float)g->w, (float)g->h);
    glUniform4f(g->u_color, r, gr, b, a);
    glUniform1f(g->u_use_tex, textured ? 1.0f : 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textured ? tex : 0);
    glEnableVertexAttribArray((GLuint)g->a_pos);
    glEnableVertexAttribArray((GLuint)g->a_uv);
    glVertexAttribPointer((GLuint)g->a_pos, 2, GL_FLOAT, GL_FALSE, 0, pos);
    glVertexAttribPointer((GLuint)g->a_uv, 2, GL_FLOAT, GL_FALSE, 0, uv);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

#else /* 桌面 GL 1.1 */

static void draw_quad(GLR* g, float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1,
                      float r, float gr, float b, float a, int textured, GLuint tex) {
    (void)g;
    glColor4f(r, gr, b, a);
    if (textured) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex);
    } else {
        glDisable(GL_TEXTURE_2D);
    }
    glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(u0, v1); glVertex2f(x, y + h);
        glTexCoord2f(u0, v0); glVertex2f(x, y);
        glTexCoord2f(u1, v1); glVertex2f(x + w, y + h);
        glTexCoord2f(u1, v0); glVertex2f(x + w, y);
    glEnd();
}

#endif

/* ---------- 字体图集 ---------- */

static bool ensure_font(GLR* g) {
    if (g->font_ready) return true;
    if (g->font_data.empty()) return false;
    g->font_ready = stbtt_InitFont(&g->font, g->font_data.data(), 0) != 0;
    return g->font_ready;
}

static void ensure_atlas(GLR* g) {
    if (g->atlas_tex) return;
    std::vector<unsigned char> blank(ATLAS_W * ATLAS_H * 4, 0);
    glGenTextures(1, &g->atlas_tex);
    glBindTexture(GL_TEXTURE_2D, g->atlas_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ATLAS_W, ATLAS_H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, blank.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void atlas_reset(GLR* g) {
    std::vector<unsigned char> blank(ATLAS_W * ATLAS_H * 4, 0);
    glBindTexture(GL_TEXTURE_2D, g->atlas_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ATLAS_W, ATLAS_H,
                    GL_RGBA, GL_UNSIGNED_BYTE, blank.data());
    g->glyphs.clear();
    g->atlas_x = 1; g->atlas_y = 1; g->atlas_row_h = 0;
}

static const Glyph& glyph_get(GLR* g, unsigned cp, int px) {
    unsigned long long key = ((unsigned long long)cp << 20) | (unsigned)px;
    auto it = g->glyphs.find(key);
    if (it != g->glyphs.end()) return it->second;

    Glyph gl;
    float scale = stbtt_ScaleForPixelHeight(&g->font, (float)px);
    int gi = stbtt_FindGlyphIndex(&g->font, (int)cp);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBox(&g->font, gi, scale, scale, &x0, &y0, &x1, &y1);
    int gw = x1 - x0, gh = y1 - y0;
    int adv = 0;
    stbtt_GetGlyphHMetrics(&g->font, gi, &adv, nullptr);
    gl.advance = adv * scale;
    gl.xoff = (float)x0;
    gl.yoff = (float)y0;

    if (gw > 0 && gh > 0) {
        if (g->atlas_x + gw + 1 >= ATLAS_W) {
            g->atlas_x = 1;
            g->atlas_y += g->atlas_row_h + 1;
            g->atlas_row_h = 0;
        }
        if (g->atlas_y + gh + 1 >= ATLAS_H) atlas_reset(g);

        std::vector<unsigned char> bmp(gw * gh), rgba(gw * gh * 4, 255);
        stbtt_MakeGlyphBitmap(&g->font, bmp.data(), gw, gh, gw, scale, scale, gi);
        for (int i = 0; i < gw * gh; ++i) {
            rgba[i * 4 + 0] = 255;
            rgba[i * 4 + 1] = 255;
            rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = bmp[i];
        }
        glBindTexture(GL_TEXTURE_2D, g->atlas_tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, g->atlas_x, g->atlas_y, gw, gh,
                        GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        gl.x = g->atlas_x; gl.y = g->atlas_y; gl.w = gw; gl.h = gh;
        g->atlas_x += gw + 1;
        if (gh > g->atlas_row_h) g->atlas_row_h = gh;
    }
    return g->glyphs.emplace(key, gl).first->second;
}

/* 返回字形绘制后的 x 偏移; measure=1 时只测量 */
static float text_run(GLR* g, const char* utf8, float x, float y, float px,
                      float r, float gr, float b, float a, int measure) {
    if (!ensure_font(g)) return 0;
    ensure_atlas(g);
    float scale = stbtt_ScaleForPixelHeight(&g->font, px);
    int ascent = 0;
    stbtt_GetFontVMetrics(&g->font, &ascent, nullptr, nullptr);
    float baseline = y + ascent * scale;

    float cx = x;
    const unsigned char* p = (const unsigned char*)utf8;
    while (*p) {
        unsigned cp = 0xFFFD;
        size_t len = 1;
        if ((*p & 0x80) == 0) { cp = *p; len = 1; }
        else if ((*p & 0xE0) == 0xC0 && p[1]) { cp = ((*p & 0x1F) << 6) | (p[1] & 0x3F); len = 2; }
        else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
            cp = ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); len = 3;
        } else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
            cp = ((*p & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
            len = 4;
        }
        p += len;

        const Glyph& gl = glyph_get(g, cp, (int)px);
        if (!measure && gl.w > 0 && gl.h > 0) {
            /* 图集自上而下: 顶行 v = y/H */
            float u0 = (float)gl.x / ATLAS_W;
            float u1 = (float)(gl.x + gl.w) / ATLAS_W;
            float v0 = (float)gl.y / ATLAS_H;
            float v1 = (float)(gl.y + gl.h) / ATLAS_H;
            draw_quad(g, cx + gl.xoff, baseline + gl.yoff, (float)gl.w, (float)gl.h,
                      u0, v0, u1, v1, r, gr, b, a, 1, g->atlas_tex);
        }
        cx += gl.advance;
    }
    return cx - x;
}

/* ---------- vtable 实现 ---------- */

static GLR* to_glr(ogal_renderer* r) { return (GLR*)r; }

static int r_init(ogal_renderer* r, void* native_window) {
    GLR* g = to_glr(r);
#ifdef OGAL_GLES2
    g->dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g->dpy == EGL_NO_DISPLAY) return 1;
    if (!eglInitialize(g->dpy, nullptr, nullptr)) return 2;
    EGLint attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLint n = 0;
    if (!eglChooseConfig(g->dpy, attrs, &g->config, 1, &n) || n < 1) return 3;
    g->surf = eglCreateWindowSurface(g->dpy, g->config, (EGLNativeWindowType)native_window, nullptr);
    if (g->surf == EGL_NO_SURFACE) return 4;
    EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    g->ctx = eglCreateContext(g->dpy, g->config, EGL_NO_CONTEXT, ctx_attrs);
    if (g->ctx == EGL_NO_CONTEXT) return 5;
    if (!eglMakeCurrent(g->dpy, g->surf, g->surf, g->ctx)) return 6;
    gles_init_shaders(g);
#else
    g->hwnd = (HWND)native_window;
    g->hdc = GetDC(g->hwnd);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    int fmt = ChoosePixelFormat(g->hdc, &pfd);
    if (!fmt) return 1;
    if (!SetPixelFormat(g->hdc, fmt, &pfd)) return 2;
    g->hrc = wglCreateContext(g->hdc);
    if (!g->hrc) return 3;
    if (!wglMakeCurrent(g->hdc, g->hrc)) return 4;
#endif
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return 0;
}

static void r_shutdown(ogal_renderer* r) {
    (void)r; /* 上下文销毁见 destroy */
}

static void r_resize(ogal_renderer* r, int w, int h) {
    GLR* g = to_glr(r);
    g->w = w; g->h = h;
}

static void* r_create_texture(ogal_renderer* r, const unsigned char* rgba, int w, int h) {
    GLR* g = to_glr(r);
    /* 翻转行序为 GL 习惯的自下而上 */
    std::vector<unsigned char> flipped((size_t)w * h * 4);
    for (int y = 0; y < h; ++y)
        memcpy(&flipped[(size_t)(h - 1 - y) * w * 4], &rgba[(size_t)y * w * 4], (size_t)w * 4);

    Tex* t = new Tex();
    glGenTextures(1, &t->id);
    glBindTexture(GL_TEXTURE_2D, t->id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    t->w = w; t->h = h;
    return t;
}

static void r_destroy_texture(ogal_renderer* r, void* tex) {
    (void)r;
    if (!tex) return;
    Tex* t = (Tex*)tex;
    glDeleteTextures(1, &t->id);
    delete t;
}

static void r_draw_texture(ogal_renderer* r, void* tex, float x, float y, float w, float h, float alpha) {
    GLR* g = to_glr(r);
    Tex* t = (Tex*)tex;
    if (!t) return;
    /* 纹理为自下而上: 顶行 v=1, 底行 v=0 */
    draw_quad(g, x, y, w, h, 0, 1, 1, 0, 1, 1, 1, alpha, 1, t->id);
}

static void r_fill_rect(ogal_renderer* r, float x, float y, float w, float h,
                        float cr, float cg, float cb, float ca) {
    GLR* g = to_glr(r);
    draw_quad(g, x, y, w, h, 0, 0, 0, 0, cr, cg, cb, ca, 0, 0);
}

static void r_draw_text(ogal_renderer* r, const char* utf8, float x, float y, float px,
                        float cr, float cg, float cb, float ca) {
    GLR* g = to_glr(r);
    text_run(g, utf8, x, y, px, cr, cg, cb, ca, 0);
}

static float r_text_width(ogal_renderer* r, const char* utf8, float px) {
    GLR* g = to_glr(r);
    return text_run(g, utf8, 0, 0, px, 1, 1, 1, 1, 1);
}

static int r_set_font(ogal_renderer* r, const unsigned char* ttf_data, int len) {
    GLR* g = to_glr(r);
    g->font_data.assign(ttf_data, ttf_data + len);
    g->font_ready = false;
    g->glyphs.clear();
    return ensure_font(g) ? 0 : 1;
}

static void r_begin(ogal_renderer* r) {
    GLR* g = to_glr(r);
    glViewport(0, 0, g->w, g->h);
#ifdef OGAL_GLES2
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
#else
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, g->w, g->h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
#endif
}

static void r_present(ogal_renderer* r) {
    GLR* g = to_glr(r);
#ifdef OGAL_GLES2
    eglSwapBuffers(g->dpy, g->surf);
#else
    SwapBuffers(g->hdc);
#endif
}

static void r_destroy(ogal_renderer* r) {
    GLR* g = to_glr(r);
#ifdef OGAL_GLES2
    if (g->dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(g->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g->ctx != EGL_NO_CONTEXT) eglDestroyContext(g->dpy, g->ctx);
        if (g->surf != EGL_NO_SURFACE) eglDestroySurface(g->dpy, g->surf);
        eglTerminate(g->dpy);
    }
#else
    if (g->hrc) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(g->hrc); }
    if (g->hwnd && g->hdc) ReleaseDC(g->hwnd, g->hdc);
#endif
    delete g;
}

} // namespace

extern "C" ogal_renderer* ogal_create_gl_renderer(void) {
    GLR* g = new GLR();
    g->base.ud = g;
    g->base.init = r_init;
    g->base.shutdown = r_shutdown;
    g->base.resize = r_resize;
    g->base.create_texture = r_create_texture;
    g->base.destroy_texture = r_destroy_texture;
    g->base.draw_texture = r_draw_texture;
    g->base.fill_rect = r_fill_rect;
    g->base.draw_text = r_draw_text;
    g->base.text_width = r_text_width;
    g->base.set_font = r_set_font;
    g->base.begin = r_begin;
    g->base.present = r_present;
    g->base.destroy = r_destroy;
    return &g->base;
}

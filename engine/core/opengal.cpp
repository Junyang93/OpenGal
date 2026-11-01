/*
 * OpenGal 引擎主体
 * 职责: VM 宿主桥接 / 舞台状态 / 纹理缓存 / 对话框与选项布局 / 淡入淡出。
 * 平台差异全部经由 ogal_platform / ogal_renderer / ogal_audio 三个 vtable 注入。
 */
#include "opengal.h"
#include "gal_script.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {

struct Sprite {
    std::string key;
    void* tex = nullptr;
    float pos = 0.5f; /* 水平中心 0~1 */
};

struct Engine : public gal::IHost {
    ogal_platform pf;
    ogal_renderer* r = nullptr;
    ogal_audio* a = nullptr;

    int w = 0, h = 0;
    bool loaded = false;
    std::string last_err;

    /* 舞台 */
    std::map<std::string, void*> tex_cache; /* path -> texture */
    std::string bg_path;
    void* bg_tex = nullptr;
    std::vector<Sprite> sprites;

    /* 淡入淡出(黑场) */
    float fade = 0.0f;          /* 0=全透明 1=全黑 */
    float fade_target = 0.0f;
    static constexpr float FADE_SPEED = 2.0f; /* 每秒 */

    /* 对话 / 选项 */
    bool say_active = false;
    std::string speaker, text;
    bool choice_active = false;
    std::vector<std::string> choices;
    std::vector<float> cr; /* x,y,w,h 依次 */

    gal::ScriptVM vm;

    /* ---------- 工具 ---------- */
    void logf(int level, const std::string& msg) {
        if (pf.log) pf.log(pf.ud, level, msg.c_str());
    }

    uint64_t now() const { return pf.time_ms ? pf.time_ms(pf.ud) : 0; }

    void* load_texture(const std::string& path) {
        auto it = tex_cache.find(path);
        if (it != tex_cache.end()) return it->second;
        size_t len = 0;
        void* data = pf.read_file ? pf.read_file(pf.ud, path.c_str(), &len) : nullptr;
        if (!data) {
            logf(1, ("资源缺失: " + path).c_str());
            last_err = "资源缺失: " + path;
            return nullptr;
        }
        int iw = 0, ih = 0, comp = 0;
        unsigned char* px = stbi_load_from_memory((const unsigned char*)data, (int)len,
                                                  &iw, &ih, &comp, 4);
        free(data);
        if (!px) {
            logf(1, ("图片解码失败: " + path).c_str());
            last_err = "图片解码失败: " + path;
            return nullptr;
        }
        void* tex = r->create_texture(r, px, iw, ih);
        stbi_image_free(px);
        tex_cache[path] = tex; /* 允许 tex=nullptr 缓存失败, 避免重复报错 */
        tex_size[path] = {iw, ih};
        return tex;
    }

    void play_audio_file(const std::string& path, int is_music, int loop) {
        if (!a) return;
        size_t len = 0;
        void* data = pf.read_file ? pf.read_file(pf.ud, path.c_str(), &len) : nullptr;
        if (!data) { logf(1, ("音频缺失: " + path).c_str()); return; }
        if (is_music) a->play_music(a, (const unsigned char*)data, (int)len, loop);
        else a->play_sound(a, (const unsigned char*)data, (int)len);
        free(data); /* 后端同步拷贝所需数据 */
    }

    /* ---------- VM 宿主 ---------- */
    void onScene(const std::string& path, bool fade_in) override {
        void* tex = load_texture(path);
        if (tex) {
            bg_path = path;
            bg_tex = tex;
            /* 换背景时清立绘(常见演出习惯) */
            sprites.clear();
        }
        if (fade_in) { fade = 1.0f; fade_target = 0.0f; }
    }
    void onShow(const std::string& path, float pos, bool fade_in) override {
        void* tex = load_texture(path);
        if (!tex) return;
        /* 同名 key 视为换图 */
        for (auto& s : sprites) {
            if (s.key == path) { s.tex = tex; s.pos = pos; return; }
        }
        sprites.push_back(Sprite{path, tex, pos});
        (void)fade_in; /* 立绘淡入留待后续版本 */
    }
    void onHide(const std::string& path) override {
        for (size_t i = 0; i < sprites.size(); ++i) {
            if (sprites[i].key == path) { sprites.erase(sprites.begin() + (long)i); return; }
        }
    }
    void onPlayMusic(const std::string& path, bool loop) override { play_audio_file(path, 1, loop ? 1 : 0); }
    void onPlaySound(const std::string& path) override { play_audio_file(path, 0, 0); }
    void onStopMusic() override { if (a) a->stop_music(a); }

    /* ---------- 布局 ---------- */
    static float font_text(float px) { return px; }
    float ui_text_px() const { return (float)h * 0.030f; }
    float ui_speaker_px() const { return (float)h * 0.034f; }

    std::vector<std::string> wrap_text(const std::string& s, float max_w, float px) {
        std::vector<std::string> lines;
        std::string cur;
        /* 按 UTF-8 字符切分(CJK 逐字, 拉丁按词太复杂, 逐字可接受) */
        size_t i = 0;
        while (i < s.size()) {
            size_t len = 1;
            unsigned char c = (unsigned char)s[i];
            if ((c & 0x80) == 0) len = 1;
            else if ((c & 0xE0) == 0xC0) len = 2;
            else if ((c & 0xF0) == 0xE0) len = 3;
            else if ((c & 0xF8) == 0xF0) len = 4;
            std::string ch = s.substr(i, len);
            i += len;
            if (ch == "\n") { lines.push_back(cur); cur.clear(); continue; }
            std::string trial = cur + ch;
            if (!cur.empty() && r->text_width(r, trial.c_str(), px) > max_w) {
                lines.push_back(cur);
                cur = ch;
            } else {
                cur = trial;
            }
        }
        if (!cur.empty()) lines.push_back(cur);
        return lines;
    }

    void draw_dialogue() {
        float mx = w * 0.04f;
        float box_h = h * 0.26f;
        float box_y = h - box_h - h * 0.02f;
        r->fill_rect(r, mx, box_y, w - 2 * mx, box_h, 0, 0, 0.08f, 0.72f);

        float pad = h * 0.018f;
        float ty = box_y + pad;
        float tx = mx + pad;
        float max_w = w - 2 * mx - 2 * pad;

        if (!speaker.empty()) {
            r->draw_text(r, speaker.c_str(), tx, ty, ui_speaker_px(), 1.0f, 0.85f, 0.6f, 1.0f);
            ty += ui_speaker_px() * 1.35f;
        }
        auto lines = wrap_text(text, max_w, ui_text_px());
        float lh = ui_text_px() * 1.30f;
        for (const auto& ln : lines) {
            if (ty + ui_text_px() > box_y + box_h - pad * 0.5f) break;
            r->draw_text(r, ln.c_str(), tx, ty, ui_text_px(), 1, 1, 1, 1.0f);
            ty += lh;
        }
        /* 继续提示 */
        float cx0 = w - mx - h * 0.02f;
        r->draw_text(r, "\xe2\x96\xbc", cx0, box_y + box_h - pad * 1.2f, ui_text_px() * 0.8f,
                     1, 1, 1, 0.9f);
    }

    void draw_choices() {
        size_t n = choices.size();
        if (!n) return;
        float btn_h = h * 0.075f;
        float gap = h * 0.02f;
        float total = (float)n * btn_h + (float)(n - 1) * gap;
        float start_y = (h - total) * 0.5f;
        float px = ui_text_px();
        cr.clear();
        for (size_t i = 0; i < n; ++i) {
            float tw = r->text_width(r, choices[i].c_str(), px);
            float bw = tw + h * 0.06f;
            if (bw < w * 0.3f) bw = w * 0.3f;
            if (bw > w * 0.86f) bw = w * 0.86f;
            float bx = (w - bw) * 0.5f;
            float by = start_y + (float)i * (btn_h + gap);
            r->fill_rect(r, bx, by, bw, btn_h, 0.12f, 0.16f, 0.28f, 0.92f);
            float tx = bx + (bw - tw) * 0.5f;
            float ty = by + (btn_h - px) * 0.5f;
            r->draw_text(r, choices[i].c_str(), tx, ty, px, 1, 1, 1, 1.0f);
            cr.insert(cr.end(), {bx, by, bw, btn_h});
        }
    }

    void draw_stage() {
        if (bg_tex) {
            /* cover 铺满 */
            int tw = 0, th = 0;
            /* 纹理尺寸: 由渲染器在 userdata 中维护, 这里用近似: 从缓存查不到尺寸,
             * 改为引擎记录。简化: 记录加载时的尺寸。 */
            auto it = tex_size.find(bg_path);
            if (it != tex_size.end()) {
                float tw0 = (float)it->second.first, th0 = (float)it->second.second;
                float sc = (w / tw0) > (h / th0) ? (w / tw0) : (h / th0);
                float dw = tw0 * sc, dh = th0 * sc;
                r->draw_texture(r, bg_tex, (w - dw) * 0.5f, (h - dh) * 0.5f, dw, dh, 1.0f);
            } else {
                r->draw_texture(r, bg_tex, 0, 0, (float)w, (float)h, 1.0f);
            }
        } else {
            r->fill_rect(r, 0, 0, (float)w, (float)h, 0, 0, 0, 1);
        }
        float sh = h * 0.85f;
        for (const auto& s : sprites) {
            auto it = tex_size.find(s.key);
            float sw = sh; /* 默认方形 */
            if (it != tex_size.end()) sw = sh * ((float)it->second.first / (float)it->second.second);
            float sx = s.pos * w - sw * 0.5f;
            r->draw_texture(r, s.tex, sx, h - sh, sw, sh, 1.0f);
        }
        if (fade > 0.001f) r->fill_rect(r, 0, 0, (float)w, (float)h, 0, 0, 0, fade);
    }
    std::map<std::string, std::pair<int, int>> tex_size;
};

} // namespace

/* ================= 公开 API ================= */

extern "C" {

ogal_engine* ogal_create(const ogal_platform* pf, const char* renderer_hint,
                         const char* audio_hint) {
    Engine* e = new Engine();
    e->pf = *pf;
    if (!renderer_hint || strcmp(renderer_hint, "gl") == 0)
        e->r = ogal_create_gl_renderer();
    if (audio_hint) {
        if (strcmp(audio_hint, "null") == 0) e->a = ogal_create_null_audio();
        else if (strcmp(audio_hint, "opensl") == 0) e->a = ogal_create_opensl_audio();
    }
    if (!e->a) e->a = ogal_create_null_audio();
    if (e->a && e->a->init) e->a->init(e->a);
    return (ogal_engine*)e;
}

void ogal_destroy(ogal_engine* eng) {
    Engine* e = (Engine*)eng;
    if (!e) return;
    if (e->r && e->r->destroy) e->r->destroy(e->r);
    if (e->a && e->a->destroy) e->a->destroy(e->a);
    delete e;
}

int ogal_load(ogal_engine* eng, const char* script_path) {
    Engine* e = (Engine*)eng;
    size_t len = 0;
    void* data = e->pf.read_file(e->pf.ud, script_path, &len);
    if (!data) return 1;
    std::string src((const char*)data, len);
    free(data);
    std::string err;
    if (!e->vm.load(src, err)) {
        e->last_err = err;
        if (e->pf.log) e->pf.log(e->pf.ud, 2, err.c_str());
        return 2;
    }
    /* 可选字体: 剧本目录下 font/default.ttf */
    if (e->r && e->r->set_font) {
        size_t flen = 0;
        void* fdata = e->pf.read_file(e->pf.ud, "font/default.ttf", &flen);
        if (fdata) {
            e->r->set_font(e->r, (const unsigned char*)fdata, (int)flen);
            free(fdata);
        }
    }
    e->loaded = true;
    return 0;
}

ogal_event_type ogal_step(ogal_engine* eng, ogal_event* out_evt) {
    Engine* e = (Engine*)eng;
    if (!e->loaded) return OGAL_EVT_END;
    e->vm.set_now(e->now());
    const std::string *sp = nullptr, *tx = nullptr;
    std::string err;
    gal::ScriptVM::Suspend s = e->vm.step(*e, sp, tx, err);
    if (!err.empty() && e->pf.log) e->pf.log(e->pf.ud, 2, err.c_str());

    e->say_active = (s == gal::ScriptVM::SUSP_SAY);
    e->choice_active = (s == gal::ScriptVM::SUSP_CHOICE);
    if (out_evt) {
        memset(out_evt, 0, sizeof(*out_evt));
        if (s == gal::ScriptVM::SUSP_SAY) {
            out_evt->type = OGAL_EVT_SAY;
            out_evt->speaker = sp ? sp->c_str() : nullptr;
            out_evt->text = tx ? tx->c_str() : "";
        } else if (s == gal::ScriptVM::SUSP_CHOICE) {
            out_evt->type = OGAL_EVT_CHOICE;
            e->choices = e->vm.choice_texts();
            out_evt->choice_count = (int)e->choices.size();
            for (size_t i = 0; i < e->choices.size() && i < 16; ++i)
                out_evt->choices[i] = e->choices[i].c_str();
        } else if (s == gal::ScriptVM::SUSP_END) {
            out_evt->type = OGAL_EVT_END;
        } else {
            out_evt->type = OGAL_EVT_NONE;
        }
    }
    switch (s) {
        case gal::ScriptVM::SUSP_SAY: return OGAL_EVT_SAY;
        case gal::ScriptVM::SUSP_CHOICE: return OGAL_EVT_CHOICE;
        case gal::ScriptVM::SUSP_END: return OGAL_EVT_END;
        default: return OGAL_EVT_NONE;
    }
}

void ogal_on_tap(ogal_engine* eng, float x, float y) {
    Engine* e = (Engine*)eng;
    /* 选择状态: 命中检测基于最近一次渲染的选项矩形 */
    if (e->choice_active) {
        for (size_t i = 0; i + 3 < e->cr.size(); i += 4) {
            if (x >= e->cr[i] && x <= e->cr[i] + e->cr[i+2] &&
                y >= e->cr[i+1] && y <= e->cr[i+1] + e->cr[i+3]) {
                e->vm.choose((int)i / 4);
                return;
            }
        }
        return; /* 未命中选项: 忽略 */
    }
    if (e->say_active) e->vm.advance();
}

void ogal_render(ogal_engine* eng) {
    Engine* e = (Engine*)eng;
    if (!e->r) return;

    /* 推进淡入淡出 */
    if (e->fade != e->fade_target) {
        float dt = 1.0f / 60.0f; /* render 每帧调用一次 */
        float d = e->fade_target - e->fade;
        float step = e->FADE_SPEED * dt;
        if (fabsf(d) <= step) e->fade = e->fade_target;
        else e->fade += (d > 0 ? step : -step);
    }

    e->r->begin(e->r);
    e->draw_stage();
    if (e->choice_active) e->draw_choices();
    else if (e->say_active) e->draw_dialogue();
    e->r->present(e->r);
}

void ogal_resize(ogal_engine* eng, int w, int h) {
    Engine* e = (Engine*)eng;
    e->w = w; e->h = h;
    if (e->r && e->r->resize) e->r->resize(e->r, w, h);
}

void ogal_get_size(ogal_engine* eng, int* w, int* h) {
    Engine* e = (Engine*)eng;
    if (w) *w = e->w;
    if (h) *h = e->h;
}

ogal_renderer* ogal_engine_get_renderer(ogal_engine* eng) {
    return ((Engine*)eng)->r;
}

} // extern "C"

/* 静音音频后端: 用于桌面调试或不支持音频的平台 */
#include "opengal.h"

#include <cstdio>
#include <cstdlib>

namespace {

struct NullAudio {
    ogal_audio base;
};

int a_init(ogal_audio* a) { (void)a; return 0; }
void a_play_music(ogal_audio* a, const unsigned char* data, int len, int loop) {
    (void)a; (void)data; (void)len; (void)loop;
}
void a_play_sound(ogal_audio* a, const unsigned char* data, int len) {
    (void)a; (void)data; (void)len;
}
void a_stop_music(ogal_audio* a) { (void)a; }
void a_destroy(ogal_audio* a) { delete (NullAudio*)a; }

} // namespace

extern "C" ogal_audio* ogal_create_null_audio(void) {
    NullAudio* na = new NullAudio();
    na->base.ud = nullptr;
    na->base.init = a_init;
    na->base.play_music = a_play_music;
    na->base.play_sound = a_play_sound;
    na->base.stop_music = a_stop_music;
    na->base.destroy = a_destroy;
    return &na->base;
}

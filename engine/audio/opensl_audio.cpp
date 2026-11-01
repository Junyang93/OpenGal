/*
 * OpenSL ES 音频后端 (Android NDK 与 HarmonyOS NDK 均内置 OpenSL ES)
 *
 * 内置 RIFF/WAVE (PCM 16bit) 解码; OGG/MP3 解码为预留扩展点,
 * 替换 decode_wav() 即可获得完整音乐支持, 接口不变。
 *
 * 架构: 一个 BGM Player (可循环) + 一个音效 Player (一次性)。
 * 实现约定: play_* 调用为同步, 后端拷贝所需数据后引擎即释放原始缓冲区。
 */
#include "opengal.h"

#if defined(__ANDROID__) || defined(__OHOS__)
#define OGAL_HAVE_OPENSL 1
#endif

#ifdef OGAL_HAVE_OPENSL

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include <cstring>
#include <vector>

namespace {

struct Pcm {
    std::vector<short> data;
    int channels = 2;
    int rate = 44100;
};

/* 极简 RIFF/WAVE PCM16 解析 */
static bool decode_wav(const unsigned char* buf, int len, Pcm& out) {
    if (len < 44 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0)
        return false;
    int fmt_channels = 0, fmt_rate = 0, fmt_bits = 0;
    const unsigned char* pcm = nullptr;
    int pcm_len = 0;
    int off = 12;
    while (off + 8 <= len) {
        unsigned chunk_size = (unsigned)buf[off+4] | ((unsigned)buf[off+5] << 8) |
                              ((unsigned)buf[off+6] << 16) | ((unsigned)buf[off+7] << 24);
        if (memcmp(buf + off, "fmt ", 4) == 0 && chunk_size >= 16) {
            fmt_channels = buf[off+10] | (buf[off+11] << 8);
            fmt_rate = buf[off+12] | (buf[off+13] << 8) | (buf[off+14] << 16) | (buf[off+15] << 24);
            fmt_bits = buf[off+22] | (buf[off+23] << 8);
        } else if (memcmp(buf + off, "data", 4) == 0) {
            pcm = buf + off + 8;
            pcm_len = (int)chunk_size;
        }
        off += 8 + (int)chunk_size + (chunk_size & 1);
    }
    if (!pcm || fmt_bits != 16 || fmt_channels < 1 || fmt_rate <= 0) return false;
    out.channels = fmt_channels;
    out.rate = fmt_rate;
    int samples = pcm_len / 2;
    out.data.resize((size_t)samples);
    memcpy(out.data.data(), pcm, (size_t)pcm_len);
    return true;
}

struct SlAudio {
    ogal_audio base;
    SLObjectItf engine_obj = nullptr;
    SLEngineItf engine = nullptr;
    SLObjectItf outmix = nullptr;
    SLObjectItf player_obj = nullptr;
    SLPlayItf player = nullptr;
    SLBufferQueueItf bq = nullptr;
    SLObjectItf sfx_obj = nullptr;
    SLPlayItf sfx = nullptr;
    SLBufferQueueItf sfx_bq = nullptr;
    /* BGM 循环缓冲: 简单起见整块入队(BufferQueue 不拷贝数据, 必须保活) */
    std::vector<short> music_buf;
    std::vector<short> sfx_buf;
    int music_channels = 2;
    int music_rate = 44100;
    int music_loop = 0;
};

static void sl_pcm_fmt(SlAudio* sa, SLDataFormat_PCM* fmt, int ch, int rate) {
    fmt->formatType = SL_DATAFORMAT_PCM;
    fmt->numChannels = (SLuint32)ch;
    fmt->samplesPerSec = (SLuint32)rate * 1000;
    fmt->bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16;
    fmt->containerSize = SL_PCMSAMPLEFORMAT_FIXED_16;
    fmt->channelMask = ch == 2 ? SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT
                               : SL_SPEAKER_FRONT_CENTER;
    fmt->endianness = SL_BYTEORDER_LITTLEENDIAN;
    (void)sa;
}

static int create_player(SlAudio* sa, SLObjectItf* obj, SLPlayItf* play,
                         SLBufferQueueItf* bq, int ch, int rate) {
    SLDataFormat_PCM fmt;
    sl_pcm_fmt(sa, &fmt, ch, rate);
    SLDataLocator_BufferQueue loc = { SL_DATALOCATOR_BUFFERQUEUE, 2 };
    SLDataSource src = { &loc, &fmt };
    SLDataLocator_OutputMix outloc = { SL_DATALOCATOR_OUTPUTMIX, sa->outmix };
    SLDataSink sink = { &outloc, nullptr };
    const SLInterfaceID ids[] = { SL_IID_BUFFERQUEUE, SL_IID_PLAY };
    const SLboolean req[] = { SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE };
    if ((*sa->engine)->CreateAudioPlayer(sa->engine, obj, &src, &sink, 2, ids, req) != SL_RESULT_SUCCESS)
        return 1;
    if ((*obj)->Realize(*obj, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS) return 2;
    (*obj)->GetInterface(*obj, SL_IID_PLAY, (void*)play);
    (*obj)->GetInterface(*obj, SL_IID_BUFFERQUEUE, (void*)bq);
    return 0;
}

static void bq_callback(SLBufferQueueItf bq, void* ctx) {
    SlAudio* sa = (SlAudio*)ctx;
    if (!sa->music_loop || sa->music_buf.empty()) return;
    (*bq)->Enqueue(bq, sa->music_buf.data(), sa->music_buf.size() * 2);
}

int a_init(ogal_audio* a) {
    SlAudio* sa = (SlAudio*)a->ud;
    if (slCreateEngine(&sa->engine_obj, 0, nullptr, 0, nullptr, nullptr) != SL_RESULT_SUCCESS)
        return 1;
    if ((*sa->engine_obj)->Realize(sa->engine_obj, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS)
        return 2;
    (*sa->engine_obj)->GetInterface(sa->engine_obj, SL_IID_ENGINE, (void*)&sa->engine);
    if ((*sa->engine)->CreateOutputMix(sa->engine, &sa->outmix, 0, nullptr, nullptr) != SL_RESULT_SUCCESS)
        return 3;
    (*sa->outmix)->Realize(sa->outmix, SL_BOOLEAN_FALSE);
    return 0;
}

void a_play_music(ogal_audio* a, const unsigned char* data, int len, int loop) {
    SlAudio* sa = (SlAudio*)a->ud;
    Pcm pcm;
    if (!decode_wav(data, len, pcm)) return;
    sa->music_buf = pcm.data;
    sa->music_channels = pcm.channels;
    sa->music_rate = pcm.rate;
    sa->music_loop = loop;

    if (sa->player_obj) {
        (*sa->player)->SetPlayState(sa->player, SL_PLAYSTATE_STOPPED);
        (*sa->player_obj)->Destroy(sa->player_obj);
        sa->player_obj = nullptr;
    }
    if (create_player(sa, &sa->player_obj, &sa->player, &sa->bq,
                      pcm.channels, pcm.rate) != 0) return;
    (*sa->bq)->RegisterCallback(sa->bq, bq_callback, sa);
    (*sa->player)->SetPlayState(sa->player, SL_PLAYSTATE_PLAYING);
    (*sa->bq)->Enqueue(sa->bq, sa->music_buf.data(), sa->music_buf.size() * 2);
}

void a_play_sound(ogal_audio* a, const unsigned char* data, int len) {
    SlAudio* sa = (SlAudio*)a->ud;
    Pcm pcm;
    if (!decode_wav(data, len, pcm)) return;
    if (sa->sfx_obj) {
        (*sa->sfx)->SetPlayState(sa->sfx, SL_PLAYSTATE_STOPPED);
        (*sa->sfx_obj)->Destroy(sa->sfx_obj);
        sa->sfx_obj = nullptr;
    }
    if (create_player(sa, &sa->sfx_obj, &sa->sfx, &sa->sfx_bq,
                      pcm.channels, pcm.rate) != 0) return;
    (*sa->sfx)->SetPlayState(sa->sfx, SL_PLAYSTATE_PLAYING);
    /* BufferQueue 不拷贝数据: 存入成员保活 */
    sa->sfx_buf = pcm.data;
    (*sa->sfx_bq)->Enqueue(sa->sfx_bq, sa->sfx_buf.data(), sa->sfx_buf.size() * 2);
}

void a_stop_music(ogal_audio* a) {
    SlAudio* sa = (SlAudio*)a->ud;
    if (sa->player) (*sa->player)->SetPlayState(sa->player, SL_PLAYSTATE_STOPPED);
    sa->music_loop = 0;
}

void a_destroy(ogal_audio* a) {
    SlAudio* sa = (SlAudio*)a->ud;
    if (sa->player_obj) (*sa->player_obj)->Destroy(sa->player_obj);
    if (sa->sfx_obj) (*sa->sfx_obj)->Destroy(sa->sfx_obj);
    if (sa->outmix) (*sa->outmix)->Destroy(sa->outmix);
    if (sa->engine_obj) (*sa->engine_obj)->Destroy(sa->engine_obj);
    delete sa;
}

} // namespace

extern "C" ogal_audio* ogal_create_opensl_audio(void) {
    SlAudio* sa = new SlAudio();
    sa->base.ud = sa;
    sa->base.init = a_init;
    sa->base.play_music = a_play_music;
    sa->base.play_sound = a_play_sound;
    sa->base.stop_music = a_stop_music;
    sa->base.destroy = a_destroy;
    return &sa->base;
}

#else /* 无 OpenSL ES 的平台: 退化为静音 */

extern "C" ogal_audio* ogal_create_opensl_audio(void) {
    return ogal_create_null_audio();
}

#endif

/*
 * OpenGal 剧本系统: 编译 + 字节码 VM
 *
 * 语法 (Ren'Py 风格, 缩进分层):
 *   label NAME
 *   scene PATH [fade]
 *   show PATH [at left|center|right] [fade]
 *   hide PATH
 *   说话人 "文本"          # 或单独一行 "文本" 表示旁白
 *   play music PATH [loop]
 *   play sound PATH
 *   stop music
 *   set VAR = 数字|变量
 *   if VAR == 数字: 块 ... [elif ...:] [else:]
 *   menu:
 *       "选项A":
 *           ...
 *       "选项B" -> label名
 *   wait MS
 *   jump NAME / call NAME / return
 *
 * 完整参考见 docs/script.md
 */
#ifndef GAL_SCRIPT_H
#define GAL_SCRIPT_H

#include <map>
#include <string>
#include <vector>

namespace gal {

/* 引擎(宿主)回调: VM 执行到资源/表现指令时调用 */
struct IHost {
    virtual ~IHost() = default;
    virtual void onScene(const std::string& path, bool fade) = 0;
    virtual void onShow(const std::string& path, float pos, bool fade) = 0;
    virtual void onHide(const std::string& path) = 0;
    virtual void onPlayMusic(const std::string& path, bool loop) = 0;
    virtual void onPlaySound(const std::string& path) = 0;
    virtual void onStopMusic() = 0;
};

enum OpCode {
    OP_SCENE,       /* a=path, x=fade */
    OP_SHOW,        /* a=path, x=pos(0..1), y=fade */
    OP_HIDE,        /* a=path */
    OP_SAY,         /* a=speaker(可空), b=text */
    OP_CHOICE,      /* x=选项数; 后跟 x 条 OP_CHOICE_ITEM */
    OP_CHOICE_ITEM, /* a=text, jump=目标 */
    OP_PLAY_MUSIC,  /* a=path, x=loop */
    OP_PLAY_SOUND,  /* a=path */
    OP_STOP_MUSIC,
    OP_JUMP,        /* a=label */
    OP_CALL,        /* a=label */
    OP_RETURN,
    OP_SET,         /* a=var, b=值(数字字面量或以'$'前缀的变量名) */
    OP_IF,          /* a=var, b=值, x=比较符, jump=不满足时跳转 */
    OP_WAIT,        /* x=毫秒 */
    OP_END
};

enum CmpOp { CMP_EQ, CMP_NE, CMP_LT, CMP_LE, CMP_GT, CMP_GE };

struct Instruction {
    OpCode op;
    std::string a, b;
    int x = 0;
    int y = 0;
    int jump = -1;
    int line = 0;
};

struct Program {
    std::vector<Instruction> code;
    std::map<std::string, int> labels;
};

/* 编译剧本源码为字节码, 失败返回 false 并写 err */
bool compile(const std::string& source, Program& out, std::string& err);

class ScriptVM {
public:
    /* 编译并定位入口; 失败返回 false 并写 err */
    bool load(const std::string& source, std::string& err);
    void reset();

    enum Suspend { SUSP_NONE, SUSP_SAY, SUSP_CHOICE, SUSP_END };

    /* 执行指令直到阻塞。事件数据通过 out 参数带出(指针仅在返回后有效至下次 step)。 */
    Suspend step(IHost& host, const std::string*& out_speaker,
                 const std::string*& out_text, std::string& err);

    /* SAY 挂起状态下, 用户点击推进 */
    void advance() { if (susp_ == SUSP_SAY) susp_ = SUSP_NONE; }

    /* CHOICE 挂起状态下, 用户选择了第 index 项 */
    void choose(int index);

    /* WAIT 挂起状态检查: 由引擎用 time_ms 驱动 */
    bool wait_pending(uint64_t now_ms) const { return susp_ == SUSP_NONE && wait_until_ > now_ms; }
    void set_now(uint64_t now_ms) { now_ms_ = now_ms; }

    const std::vector<std::string>& choice_texts() const { return choice_texts_; }

private:
    Program prog_;
    size_t pc_ = 0;
    std::vector<int> call_stack_;
    std::map<std::string, int> vars_;
    std::string speaker_, text_;
    std::vector<std::string> choice_texts_;
    std::vector<int> choice_jumps_;
    Suspend susp_ = SUSP_NONE;
    uint64_t wait_until_ = 0;
    uint64_t now_ms_ = 0;
    bool end_ = false;

    int evalValue(const std::string& b, IHost& host) const;
    bool evalCond(const Instruction& ins, IHost& host) const;
};

} // namespace gal

#endif /* GAL_SCRIPT_H */

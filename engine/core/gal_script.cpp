#include "gal_script.h"

#include <cctype>
#include <cstdlib>

namespace gal {

/* ================= 词法分析 ================= */

enum TT {
    TT_IDENT, TT_STRING, TT_NUMBER, TT_COLON, TT_ARROW, TT_EQ, TT_CMP,
    TT_NL, TT_INDENT, TT_DEDENT, TT_EOF
};

struct Token {
    TT t;
    std::string s;      /* IDENT: 原文; STRING: 解码后文本; CMP: 比较符 */
    CmpOp cmp = CMP_EQ;
    int line = 0;
};

namespace {

bool is_ident_start(unsigned char c) {
    return std::isalpha(c) || c == '_' || c >= 0x80; /* 允许中文名 */
}
bool is_ident_char(unsigned char c) {
    /* 额外允许 / . - 以便资源路径作为一个 token(如 bg/classroom.png) */
    return std::isalnum(c) || c == '_' || c >= 0x80 || c == '/' || c == '.' || c == '-';
}
bool is_path_char(unsigned char c) {
    return is_ident_char(c) || c == '/' || c == '.' || c == '-' || c == '!';
}

bool tokenize(const std::string& src, std::vector<Token>& out, std::string& err) {
    std::vector<int> indents(1, 0);
    size_t pos = 0;
    int line_no = 1;

    auto emit = [&](TT t, const std::string& s, int ln) {
        Token tk; tk.t = t; tk.s = s; tk.line = ln; out.push_back(tk);
    };

    while (pos <= src.size()) {
        /* 取一行 */
        size_t eol = src.find('\n', pos);
        std::string raw = (eol == std::string::npos)
            ? src.substr(pos) : src.substr(pos, eol - pos);
        pos = (eol == std::string::npos) ? src.size() + 1 : eol + 1;
        int cur_line = line_no++;

        /* 去掉行尾 \r */
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();

        /* 去注释(引号内除外) */
        bool in_str = false;
        std::string line;
        for (size_t i = 0; i < raw.size(); ++i) {
            char c = raw[i];
            if (c == '"' && (i == 0 || raw[i-1] != '\\')) in_str = !in_str;
            if (c == '#' && !in_str) break;
            line += c;
        }

        /* 空白行直接跳过(不影响缩进) */
        bool blank = true;
        for (char c : line) if (!std::isspace((unsigned char)c)) { blank = false; break; }
        if (blank) continue;

        /* 缩进 */
        int indent = 0;
        for (char c : line) {
            if (c == ' ') indent++;
            else if (c == '\t') indent += 4;
            else break;
        }
        if (indent > indents.back()) {
            indents.push_back(indent);
            emit(TT_INDENT, "", cur_line);
        } else if (indent < indents.back()) {
            while (indents.size() > 1 && indent < indents.back()) {
                indents.pop_back();
                emit(TT_DEDENT, "", cur_line);
            }
            if (indent != indents.back()) {
                err = "第 " + std::to_string(cur_line) + " 行: 缩进层级不匹配";
                return false;
            }
        }

        /* 行内 token */
        size_t i = indent;
        while (i < line.size()) {
            char c = line[i];
            if (std::isspace((unsigned char)c)) { ++i; continue; }
            if (c == '"') {
                std::string s;
                size_t j = i + 1;
                bool closed = false;
                while (j < line.size()) {
                    char q = line[j];
                    if (q == '\\' && j + 1 < line.size()) {
                        char n = line[j + 1];
                        if (n == 'n') s += '\n';
                        else if (n == 't') s += '\t';
                        else s += n;
                        j += 2;
                        continue;
                    }
                    if (q == '"') { closed = true; ++j; break; }
                    s += q; ++j;
                }
                if (!closed) {
                    err = "第 " + std::to_string(cur_line) + " 行: 字符串未闭合";
                    return false;
                }
                Token tk; tk.t = TT_STRING; tk.s = s; tk.line = cur_line;
                out.push_back(tk);
                i = j;
                continue;
            }
            if (std::isdigit((unsigned char)c)) {
                size_t j = i;
                while (j < line.size() && std::isdigit((unsigned char)line[j])) ++j;
                emit(TT_NUMBER, line.substr(i, j - i), cur_line);
                i = j;
                continue;
            }
            if (is_ident_start((unsigned char)c)) {
                size_t j = i;
                while (j < line.size() && is_ident_char((unsigned char)line[j])) ++j;
                emit(TT_IDENT, line.substr(i, j - i), cur_line);
                i = j;
                continue;
            }
            if (c == ':') { emit(TT_COLON, "", cur_line); ++i; continue; }
            if (c == '-' && i + 1 < line.size() && line[i+1] == '>') {
                emit(TT_ARROW, "", cur_line); i += 2; continue;
            }
            if (c == '=') {
                if (i + 1 < line.size() && line[i+1] == '=') {
                    Token tk; tk.t = TT_CMP; tk.cmp = CMP_EQ; tk.line = cur_line;
                    out.push_back(tk); i += 2;
                } else { emit(TT_EQ, "", cur_line); ++i; }
                continue;
            }
            if (c == '!' && i + 1 < line.size() && line[i+1] == '=') {
                Token tk; tk.t = TT_CMP; tk.cmp = CMP_NE; tk.line = cur_line;
                out.push_back(tk); i += 2; continue;
            }
            if (c == '<' || c == '>') {
                CmpOp op = (c == '<') ? CMP_LT : CMP_GT;
                size_t j = i + 1;
                if (j < line.size() && line[j] == '=') {
                    op = (c == '<') ? CMP_LE : CMP_GE; ++j;
                }
                Token tk; tk.t = TT_CMP; tk.cmp = op; tk.line = cur_line;
                out.push_back(tk); i = j; continue;
            }
            err = "第 " + std::to_string(cur_line) + " 行: 无法识别的字符 '" +
                  std::string(1, c) + "'";
            return false;
        }
        emit(TT_NL, "", cur_line);
    }

    /* 文件结束: 补 DEDENT 与 EOF */
    while (indents.size() > 1) { indents.pop_back(); emit(TT_DEDENT, "", line_no); }
    emit(TT_EOF, "", line_no);
    return true;
}

} // namespace

/* ================= 语法分析 ================= */

namespace {

struct Parser {
    const std::vector<Token>& toks;
    size_t p = 0;
    Program* prog;
    std::string err;

    const Token& cur() const { return toks[p]; }
    bool at(TT t) const { return cur().t == t; }
    bool atKw(const char* kw) const { return cur().t == TT_IDENT && cur().s == kw; }
    const Token& advance() { return toks[p++]; }

    bool fail(int line, const std::string& msg) {
        err = "第 " + std::to_string(line) + " 行: " + msg;
        return false;
    }

    int emit(OpCode op, int line) {
        Instruction ins; ins.op = op; ins.line = line;
        prog->code.push_back(ins);
        return (int)prog->code.size() - 1;
    }
    /* jump == -2 表示 a 中存 label 名, 编译后统一解析 */
    int emitJump(OpCode op, const std::string& label, int line) {
        Instruction ins; ins.op = op; ins.a = label; ins.jump = -2; ins.line = line;
        prog->code.push_back(ins);
        return (int)prog->code.size() - 1;
    }
    void patchJump(int at, int target) { prog->code[at].jump = target; }
    int here() const { return (int)prog->code.size(); }

    bool expect(TT t, const char* what) {
        if (!at(t)) return fail(cur().line, std::string("期望 ") + what);
        advance();
        return true;
    }

    bool readPath(std::string& out) {
        /* 路径允许 ident 字符 + / . - ! */
        if (!at(TT_IDENT)) return fail(cur().line, "期望资源路径");
        const Token& t = advance();
        /* lexer 已按 ident 规则切分, 允许的路径扩展字符需在 lexer 侧放行,
         * 这里直接把 token 原文作为路径(lexer 已含 / . - 的支持) */
        out = t.s;
        return true;
    }

    bool parseValue(std::string& out, int& num, bool& is_var) {
        if (at(TT_NUMBER)) {
            num = std::atoi(advance().s.c_str());
            is_var = false;
            return true;
        }
        if (at(TT_IDENT)) {
            out = "$" + advance().s; /* 变量引用以 $ 前缀存储 */
            is_var = true;
            return true;
        }
        return fail(cur().line, "期望数字或变量名");
    }

    /* 解析 if/elif 条件体: 生成 OP_IF, 返回其位置 */
    bool parseCond(int& if_pos) {
        int line = cur().line;
        if (!expect(TT_IDENT, "变量名")) return false;
        std::string var = toks[p-1].s;
        if (!at(TT_CMP)) return fail(cur().line, "期望比较运算符(== != < <= > >=)");
        CmpOp op = advance().cmp;
        std::string vname; int vnum = 0; bool is_var;
        if (!parseValue(vname, vnum, is_var)) return false;

        if_pos = emit(OP_IF, line);
        Instruction& ins = prog->code[if_pos];
        ins.a = var; ins.x = (int)op;
        if (is_var) ins.b = vname;
        else { ins.b.clear(); ins.y = vnum; ins.x |= 0x100; } /* 立即数记在 y */
        return true;
    }

    bool parseBlock() {
        /* 吃掉块首的 NL */
        while (at(TT_NL)) advance();
        if (!expect(TT_INDENT, "缩进块")) return false;
        while (!at(TT_DEDENT)) {
            if (at(TT_EOF)) return fail(cur().line, "块未闭合");
            if (!parseStatement()) return false;
        }
        advance(); /* DEDENT */
        return true;
    }

    bool parseIf() {
        advance(); /* if */
        std::vector<int> end_jumps;
        int if_pos = -1;
        if (!parseCond(if_pos)) return false;
        if (!expect(TT_COLON, "':'") return false;
        if (!parseBlock()) return false;
        end_jumps.push_back(emit(OP_JUMP, -1));

        while (atKw("elif")) {
            patchJump(if_pos, here());
            advance();
            if (!parseCond(if_pos)) return false;
            if (!expect(TT_COLON, "':'") return false;
            if (!parseBlock()) return false;
            end_jumps.push_back(emit(OP_JUMP, -1));
        }
        if (atKw("else")) {
            patchJump(if_pos, here());
            advance();
            if (!expect(TT_COLON, "':'") return false;
            if (!parseBlock()) return false;
        } else {
            patchJump(if_pos, here());
        }
        for (int j : end_jumps) patchJump(j, here());
        return true;
    }

    bool parseMenu() {
        int line = cur().line;
        advance(); /* menu */
        menu_stack_.emplace_back();
        if (!expect(TT_COLON, "':'") return false;
        while (at(TT_NL)) advance();
        if (!expect(TT_INDENT, "缩进的选项块")) return false;

        /* 预扫描选项数: 选项文本字符串位于 menu 块的第 0 缩进层,
         * 内联块内容均在更深的缩进层 */
        int n_choices = 0;
        {
            size_t scan = p;
            int depth = 0;
            while (scan < toks.size()) {
                TT t = toks[scan].t;
                if (t == TT_EOF) return fail(line, "menu 未闭合");
                if (t == TT_INDENT) { depth++; }
                else if (t == TT_DEDENT) {
                    if (depth == 0) break;
                    depth--;
                } else if (depth == 0 && t == TT_STRING) {
                    n_choices++;
                }
                scan++;
            }
        }
        if (n_choices == 0) return fail(line, "menu 至少需要一个选项");

        /* VM 约定: OP_CHOICE 后紧跟 n 条连续的 OP_CHOICE_ITEM, 因此先占位 */
        int choice_pos = emit(OP_CHOICE, line);
        prog->code[choice_pos].x = n_choices;
        std::vector<int> item_pos;
        for (int k = 0; k < n_choices; ++k)
            item_pos.push_back(emit(OP_CHOICE_ITEM, line));
        emit(OP_JUMP, -1); /* 哨兵: choose() 跳转后不会执行到这里 */

        for (int k = 0; k < n_choices; ++k) {
            while (at(TT_NL)) advance();
            if (!at(TT_STRING)) return fail(cur().line, "menu 选项应以字符串开头");
            prog->code[item_pos[k]].a = advance().s;

            if (at(TT_COLON)) {
                advance();
                int blk = here();
                if (!parseBlock()) return false;
                patchJump(item_pos[k], blk);
                menu_stack_.back().push_back(emit(OP_JUMP, -1));
            } else if (at(TT_ARROW)) {
                advance();
                if (!expect(TT_IDENT, "label 名")) return false;
                prog->code[item_pos[k]].jump = -2;
                prog->code[item_pos[k]].b = toks[p-1].s;
                if (!expect(TT_NL, "换行")) return false;
            } else {
                return fail(cur().line, "选项后应为 ':' 块 或 '-> label'");
            }
        }
        while (at(TT_NL)) advance();
        if (!expect(TT_DEDENT, "menu 块结束")) return false;

        /* inline 选项块结尾的 JUMP 目标 = "菜单之后", 即 parseMenu 返回时的位置 */
        for (int j : menu_stack_.back()) patchJump(j, here());
        menu_stack_.pop_back();
        return true;
    }

    /* 嵌套 menu 各自的 inline 块结尾 JUMP 回填列表(栈) */
    std::vector<std::vector<int>> menu_stack_;

    bool parseStatement() {
        if (at(TT_NL)) { advance(); return true; }
        int line = cur().line;

        if (atKw("label")) {
            advance();
            if (!expect(TT_IDENT, "label 名")) return false;
            prog->labels[toks[p-1].s] = here();
            return expect(TT_NL, "换行");
        }
        if (atKw("scene")) {
            advance();
            std::string path;
            if (!readPath(path)) return false;
            int pos = emit(OP_SCENE, line);
            prog->code[pos].a = path;
            if (atKw("fade")) { prog->code[pos].x = 1; advance(); }
            return expect(TT_NL, "换行");
        }
        if (atKw("show")) {
            advance();
            std::string path;
            if (!readPath(path)) return false;
            float pos_v = 0.5f;
            bool fade = false;
            if (atKw("at")) {
                advance();
                if (atKw("left")) pos_v = 0.25f;
                else if (atKw("center")) pos_v = 0.5f;
                else if (atKw("right")) pos_v = 0.75f;
                else return fail(cur().line, "期望 left/center/right");
                advance();
            }
            if (atKw("fade")) { fade = true; advance(); }
            int pos = emit(OP_SHOW, line);
            prog->code[pos].a = path;
            prog->code[pos].x = (int)(pos_v * 1000);
            prog->code[pos].y = fade ? 1 : 0;
            return expect(TT_NL, "换行");
        }
        if (atKw("hide")) {
            advance();
            std::string path;
            if (!readPath(path)) return false;
            int pos = emit(OP_HIDE, line);
            prog->code[pos].a = path;
            return expect(TT_NL, "换行");
        }
        if (atKw("play")) {
            advance();
            if (atKw("music")) {
                advance();
                std::string path;
                if (!readPath(path)) return false;
                int pos = emit(OP_PLAY_MUSIC, line);
                prog->code[pos].a = path;
                if (atKw("loop")) { prog->code[pos].x = 1; advance(); }
            } else if (atKw("sound")) {
                advance();
                std::string path;
                if (!readPath(path)) return false;
                int pos = emit(OP_PLAY_SOUND, line);
                prog->code[pos].a = path;
            } else {
                return fail(cur().line, "期望 music 或 sound");
            }
            return expect(TT_NL, "换行");
        }
        if (atKw("stop")) {
            advance();
            if (!atKw("music")) return fail(cur().line, "期望 music");
            advance();
            emit(OP_STOP_MUSIC, line);
            return expect(TT_NL, "换行");
        }
        if (atKw("jump")) {
            advance();
            if (!expect(TT_IDENT, "label 名")) return false;
            emitJump(OP_JUMP, toks[p-1].s, line);
            return expect(TT_NL, "换行");
        }
        if (atKw("call")) {
            advance();
            if (!expect(TT_IDENT, "label 名")) return false;
            emitJump(OP_CALL, toks[p-1].s, line);
            return expect(TT_NL, "换行");
        }
        if (atKw("return")) {
            advance();
            emit(OP_RETURN, line);
            return expect(TT_NL, "换行");
        }
        if (atKw("set")) {
            advance();
            if (!expect(TT_IDENT, "变量名")) return false;
            std::string var = toks[p-1].s;
            if (!expect(TT_EQ, "'='")) return false;
            std::string vname; int vnum = 0; bool is_var;
            if (!parseValue(vname, vnum, is_var)) return false;
            int pos = emit(OP_SET, line);
            prog->code[pos].a = var;
            if (is_var) prog->code[pos].b = vname;
            else { prog->code[pos].jump = vnum; prog->code[pos].x = 1; } /* x=1 立即数 */
            return expect(TT_NL, "换行");
        }
        if (atKw("if")) return parseIf() && expectSafeNL();
        if (atKw("menu")) return parseMenu() && expectSafeNL();
        if (atKw("wait")) {
            advance();
            if (!at(TT_NUMBER)) return fail(cur().line, "期望毫秒数");
            int pos = emit(OP_WAIT, line);
            prog->code[pos].x = std::atoi(advance().s.c_str());
            return expect(TT_NL, "换行");
        }
        /* 说话人 "文本" */
        if (at(TT_IDENT) && toks[p+1].t == TT_STRING) {
            std::string speaker = advance().s;
            int pos = emit(OP_SAY, line);
            prog->code[pos].a = speaker;
            prog->code[pos].b = advance().s;
            return expect(TT_NL, "换行");
        }
        /* 旁白 "文本" */
        if (at(TT_STRING)) {
            int pos = emit(OP_SAY, line);
            prog->code[pos].b = advance().s;
            return expect(TT_NL, "换行");
        }
        return fail(line, "无法识别的语句");
    }

    bool expectSafeNL() {
        /* if/menu 块由 parseBlock 完整消费, 此处仅需容忍行尾 NL */
        while (at(TT_NL)) advance();
        return true;
    }
};

} // namespace

bool compile(const std::string& source, Program& out, std::string& err) {
    std::vector<Token> toks;
    if (!tokenize(source, toks, err)) return false;

    Parser parser{toks, 0, &out, ""};
    while (!parser.at(TT_EOF)) {
        if (!parser.parseStatement()) { err = parser.err; return false; }
    }

    /* 解析 label 跳转 */
    for (auto& ins : out.code) {
        if (ins.jump == -2) {
            std::string lname = !ins.b.empty() && ins.op == OP_CHOICE_ITEM ? ins.b : ins.a;
            auto it = out.labels.find(lname);
            if (it == out.labels.end()) {
                err = "第 " + std::to_string(ins.line) + " 行: 未定义的 label '" + lname + "'";
                return false;
            }
            ins.jump = it->second;
            ins.b.clear();
        }
    }
    return true;
}

/* ================= VM ================= */

bool ScriptVM::load(const std::string& source, std::string& err) {
    prog_ = Program();
    if (!compile(source, prog_, err)) return false;
    reset();
    return true;
}

void ScriptVM::reset() {
    pc_ = 0;
    call_stack_.clear();
    vars_.clear();
    susp_ = SUSP_NONE;
    end_ = false;
    wait_until_ = 0;
    speaker_.clear();
    text_.clear();
    choice_texts_.clear();
    choice_jumps_.clear();
}

int ScriptVM::evalValue(const std::string& b, IHost&) const {
    if (!b.empty() && b[0] == '$') {
        auto it = vars_.find(b.substr(1));
        return it == vars_.end() ? 0 : it->second;
    }
    return 0;
}

bool ScriptVM::evalCond(const Instruction& ins, IHost& host) const {
    int lv = 0;
    {
        auto it = vars_.find(ins.a);
        if (it != vars_.end()) lv = it->second;
    }
    int rv;
    if (ins.x & 0x100) rv = ins.y;             /* 立即数 */
    else rv = evalValue(ins.b, host);
    CmpOp op = (CmpOp)(ins.x & 0xff);
    switch (op) {
        case CMP_EQ: return lv == rv;
        case CMP_NE: return lv != rv;
        case CMP_LT: return lv < rv;
        case CMP_LE: return lv <= rv;
        case CMP_GT: return lv > rv;
        case CMP_GE: return lv >= rv;
    }
    return false;
}

void ScriptVM::choose(int index) {
    if (susp_ != SUSP_CHOICE) return;
    if (index < 0 || index >= (int)choice_jumps_.size()) return;
    pc_ = (size_t)choice_jumps_[index];
    susp_ = SUSP_NONE;
    choice_texts_.clear();
    choice_jumps_.clear();
}

ScriptVM::Suspend ScriptVM::step(IHost& host, const std::string*& out_speaker,
                                 const std::string*& out_text, std::string& err) {
    if (end_) return SUSP_END;
    if (susp_ == SUSP_SAY || susp_ == SUSP_CHOICE) return susp_;
    if (wait_until_ > now_ms_) return SUSP_NONE;

    auto fail = [&](const Instruction& ins, const char* msg) -> Suspend {
        err = "第 " + std::to_string(ins.line) + " 行: " + msg;
        end_ = true;
        return SUSP_END;
    };

    while (pc_ < prog_.code.size()) {
        const Instruction& ins = prog_.code[pc_];
        switch (ins.op) {
        case OP_SCENE:
            host.onScene(ins.a, ins.x != 0);
            ++pc_;
            break;
        case OP_SHOW:
            host.onShow(ins.a, ins.x / 1000.0f, ins.y != 0);
            ++pc_;
            break;
        case OP_HIDE:
            host.onHide(ins.a);
            ++pc_;
            break;
        case OP_PLAY_MUSIC:
            host.onPlayMusic(ins.a, ins.x != 0);
            ++pc_;
            break;
        case OP_PLAY_SOUND:
            host.onPlaySound(ins.a);
            ++pc_;
            break;
        case OP_STOP_MUSIC:
            host.onStopMusic();
            ++pc_;
            break;
        case OP_JUMP:
            pc_ = (size_t)ins.jump;
            break;
        case OP_CALL:
            call_stack_.push_back((int)pc_ + 1);
            pc_ = (size_t)ins.jump;
            break;
        case OP_RETURN:
            if (call_stack_.empty()) { end_ = true; return SUSP_END; }
            pc_ = (size_t)call_stack_.back();
            call_stack_.pop_back();
            break;
        case OP_SET: {
            int v = (ins.x == 1) ? ins.jump : evalValue(ins.b, host);
            vars_[ins.a] = v;
            ++pc_;
            break;
        }
        case OP_IF:
            if (evalCond(ins, host)) ++pc_;
            else pc_ = (size_t)ins.jump;
            break;
        case OP_WAIT:
            wait_until_ = now_ms_ + (uint64_t)ins.x;
            ++pc_;
            return SUSP_NONE;
        case OP_SAY:
            speaker_ = ins.a;
            text_ = ins.b;
            out_speaker = speaker_.empty() ? nullptr : &speaker_;
            out_text = &text_;
            ++pc_;
            susp_ = SUSP_SAY;
            return SUSP_SAY;
        case OP_CHOICE: {
            choice_texts_.clear();
            choice_jumps_.clear();
            for (int k = 0; k < ins.x; ++k) {
                const Instruction& item = prog_.code[pc_ + 1 + (size_t)k];
                choice_texts_.push_back(item.a);
                choice_jumps_.push_back(item.jump);
            }
            pc_ += (size_t)ins.x + 1;
            susp_ = SUSP_CHOICE;
            return SUSP_CHOICE;
        }
        case OP_CHOICE_ITEM:
            return fail(ins, "内部错误: 游离的选项项");
        case OP_END:
            end_ = true;
            return SUSP_END;
        }
    }
    end_ = true;
    return SUSP_END;
}

} // namespace gal

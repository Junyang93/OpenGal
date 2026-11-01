/*
 * VM 冒烟测试: 无渲染环境下完整执行示例剧本
 * 验证: 编译 / 对话序列 / 选项 / 变量分支 / 跳转 / 正常结束
 */
#include "gal_script.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct MockHost : gal::IHost {
    std::vector<std::string> scenes, shows, sounds;
    std::string music;
    int stops = 0;

    void onScene(const std::string& path, bool) override { scenes.push_back(path); }
    void onShow(const std::string& path, float, bool) override { shows.push_back(path); }
    void onHide(const std::string&) override {}
    void onPlayMusic(const std::string& path, bool loop) override { music = path + (loop ? " (loop)" : ""); }
    void onPlaySound(const std::string& path) override { sounds.push_back(path); }
    void onStopMusic() override { stops++; }
};

bool contains(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& x : v) if (x.find(s) != std::string::npos) return true;
    return false;
}

} // namespace

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "games/demo/script/main.gal";
    std::ifstream f(path, std::ios::binary);
    if (!f) { printf("FAIL: 无法打开 %s\n", path); return 1; }
    std::stringstream ss;
    ss << f.rdbuf();

    gal::ScriptVM vm;
    std::string err;
    if (!vm.load(ss.str(), err)) {
        printf("FAIL: 编译错误 %s\n", err.c_str());
        return 1;
    }

    MockHost host;
    std::vector<std::string> says;
    std::vector<size_t> choice_counts;
    int choices_made = 0;
    uint64_t vm_now_fake = 0;

    for (int guard = 0; guard < 2000; ++guard) {
        const std::string *sp = nullptr, *tp = nullptr;
        auto s = vm.step(host, sp, tp, err);
        if (!err.empty()) { printf("FAIL: 运行时错误 %s\n", err.c_str()); return 1; }
        if (s == gal::ScriptVM::SUSP_SAY) {
            says.push_back(sp ? *sp + ": " + *tp : *tp);
            vm.advance();
        } else if (s == gal::ScriptVM::SUSP_CHOICE) {
            choice_counts.push_back(vm.choice_texts().size());
            vm.choose(0); /* 总是选第一项 */
            choices_made++;
        } else if (s == gal::ScriptVM::SUSP_END) {
            break;
        } else {
            vm.set_now(vm_now_fake += 100);
        }
    }

    int fail = 0;
    auto check = [&](bool ok, const char* msg) {
        printf("%s %s\n", ok ? "PASS" : "FAIL", msg);
        if (!ok) fail++;
    };

    check(!says.empty() && says[0].find("晴朗的早晨") != std::string::npos, "首句旁白");
    check(contains(says, "欢迎来到 OpenGal"), "对话内容");
    check(choice_counts.size() == 1 && choice_counts[0] == 3, "menu 呈现 3 个选项");
    check(contains(says, "女主角"), "选项内联块(第一项)剧本执行");
    check(contains(says, "if 分支"), "变量与 if 分支");
    check(contains(says, "剧终"), "跳转后收尾剧本");
    check(!host.scenes.empty() && host.scenes[0] == "bg/sky.png", "scene 指令");
    check(!host.shows.empty() && host.shows[0] == "char/heroine.png", "show 指令");
    check(host.music.find("bgm/theme.wav (loop)") != std::string::npos, "play music loop");
    check(host.sounds.size() == 1 && host.sounds[0] == "se/click.wav", "play sound");

    printf("\n共 %zu 句对话, %d 次选择, scenes=%zu shows=%zu\n",
           says.size(), choices_made, host.scenes.size(), host.shows.size());
    if (fail == 0) printf("VM SMOKE TEST: ALL PASS\n");
    return fail ? 1 : 0;
}

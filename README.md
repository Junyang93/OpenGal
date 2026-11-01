# OpenGal

一个面向**多端移植**的开源 Galgame（视觉小说）引擎。核心采用 C++17 编写，通过三个 vtable 接口与平台解耦，同一套引擎代码可编译到 **Windows / Android / HarmonyOS**，也可以方便地移植到 iOS、Linux、Switch 等平台。

## 特性

- 📜 **Ren'Py 风格剧本语言**：`scene / show / 对话 / menu / if / jump`，缩进分层，中文说话人名原生支持
- ⚙️ **剧本编译为字节码**，事件驱动、非阻塞步进，天然适配移动端 UI 线程与主消息循环
- 🎨 **内置 GL 渲染器**：Windows 用 OpenGL 1.1（WGL），Android / HarmonyOS 用 OpenGL ES 2.0（EGL），文本由 stb_truetype 光栅化，支持中文
- 🔊 **OpenSL ES 音频后端**：Android 与 HarmonyOS NDK 通用（内置 WAV 解码；OGG/MP3 为预留扩展点）
- 🧩 **零依赖核心**：仅 vendored stb_image / stb_truetype，无第三方库
- 📦 **资源即文件**：游戏就是一个目录（剧本 + 图片 + 音频 + 字体），`tools/pack.py` 负责校验与同步

## 目录结构

```
engine/            引擎核心(平台无关)
  include/         公开 C API: opengal.h
  core/            剧本编译器 + VM + 引擎主体
  render/          GL 渲染器(桌面 GL1.1 / GLES2 双后端)
  audio/           音频后端(null / OpenSL ES)
platform/          平台层(每个平台一个薄壳)
  win32/           Windows: 窗口 + WGL + 消息循环
  android/         Android: NativeActivity + EGL + OpenSL ES(无 Java 代码)
  harmonyos/       HarmonyOS: XComponent + NAPI + rawfile + OpenSL ES
tools/pack.py      资源校验 / 打包 / 平台同步
games/demo/        示例游戏(含占位资源生成脚本)
docs/              架构、剧本语言、移植指南
```

## 快速开始

首次克隆后先生成示例资源（仓库不保存二进制资源）：

```bash
python tools/setup_demo.py    # 需要 Pillow 与 fontTools: pip install pillow fonttools
```

### Windows

```bash
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
./build/Release/galdemo.exe            # 默认运行 games/demo
./build/Release/galdemo.exe 路径/到/你的游戏目录
```

### Android

用 Android Studio 打开 `platform/android/`（或使用 gradle），直接运行即可。游戏资源已通过 `sourceSets` 指向 `games/demo`；换成自己的游戏：

```bash
python tools/pack.py android 路径/到/你的游戏
```

### HarmonyOS

用 DevEco Studio 打开 `platform/harmonyos/`，首次构建前同步游戏资源：

```bash
python tools/pack.py ohos 路径/到/你的游戏
```

然后连接真机或模拟器运行（暂不支持纯 x86 模拟器，请使用 arm 模拟器或真机）。

## 写一个自己的游戏

游戏目录结构：

```
mygame/
  script/main.gal      入口剧本
  bg/*.png             背景
  char/*.png           立绘(透明背景)
  bgm/*.wav            音乐
  se/*.wav             音效
  font/default.ttf     字体(可选, 推荐)
```

剧本示例：

```gal
label start
scene bg/classroom.png fade
play music bgm/soft.wav loop
show char/girl.png at center

girl "你好，这是第一句对话。"
menu:
    "去天台":
        scene bg/roof.png
        girl "风好大呀。"
    "去图书馆" -> library
label library
"旁白也可以没有说话人。"
```

完整语法见 [docs/script.md](docs/script.md)。

## 移植到新平台

引擎核心不碰任何平台 API。新增一个平台只需要：

1. 实现 `ogal_platform`（读文件 / 日志 / 时钟）
2. 调用 `ogal_engine_get_renderer()` 并用原生窗口（HWND / ANativeWindow / OHNativeWindow…）初始化内置 GL 渲染器，或自实现 `ogal_renderer`
3. 把触摸/点击转成 `ogal_on_tap(x, y)`，在循环里调用 `ogal_step` + `ogal_render`

详见 [docs/porting.md](docs/porting.md)。

## 持续集成

仓库附带 CI 工作流模板 [ci/ci.yml](ci/ci.yml)：将其复制为 `.github/workflows/ci.yml` 并提交，GitHub Actions 即会在每次 push / PR 时自动构建 Windows 演示程序、运行 VM 冒烟测试并校验示例游戏资源。

## 文档

- [架构说明](docs/architecture.md)
- [剧本语言参考](docs/script.md)
- [平台移植指南](docs/porting.md)

## 许可证

MIT，见 [LICENSE](LICENSE)。

# 架构说明

## 设计目标

1. **一份核心代码，多端编译**：引擎核心（`engine/`）只依赖 C++17 标准库，不 include 任何平台头文件。
2. **平台差异全部注入**：窗口、GL 上下文、文件系统、时钟、音频设备均通过 vtable 由平台层提供。
3. **事件驱动、非阻塞**：VM 的 `step` 在"需要用户输入"或"需要时间流逝"时立即返回，适合 Win32 消息循环，也适合 Android / HarmonyOS 的 UI 线程模型。

## 分层

```
┌─────────────────────────────────────────────┐
│ 平台层 platform/win32 | android | harmonyos  │  窗口/生命周期/输入/资源
├─────────────────────────────────────────────┤
│ 引擎核心 engine/core                          │
│   opengal.cpp    引擎主体: 舞台/对话框/选项 UI │
│   gal_script.cpp 剧本编译器 + 字节码 VM        │
├─────────────────────────────────────────────┤
│ 能力后端 engine/render + engine/audio         │
│   gl_renderer.cpp  GL1.1(WGL) / GLES2(EGL)   │
│   opensl_audio.cpp OpenSL ES (Android/OHOS)   │
│   null_audio.cpp   静音后端                   │
└─────────────────────────────────────────────┘
```

## 公开 API（engine/include/opengal.h）

三个 vtable：

- `ogal_platform`：`read_file`（返回 malloc 缓冲区，引擎用 `free` 释放）、`log`、`time_ms`
- `ogal_renderer`：纹理 / 矩形 / 文本 / begin / present；可选 `set_font`
- `ogal_audio`：`play_music` / `play_sound` / `stop_music`

引擎主循环（所有平台一致）：

```c
ogal_event ev;
ogal_event_type t = ogal_step(eng, &ev);     // 首次
for (;;) {
    // 平台消息/事件处理; 点击时调用 ogal_on_tap(eng, x, y)
    ogal_render(eng);                          // 每帧
    if (t == OGAL_EVT_NONE)  sleep(16ms);      // wait / 淡入淡出进行中
    else                     等待输入;
    t = ogal_step(eng, &ev);
}
```

返回值含义：

| 事件 | 含义 | 平台行为 |
|---|---|---|
| `OGAL_EVT_NONE` | VM 在 `wait` 或淡入淡出 | 16ms 再次 `step` |
| `OGAL_EVT_SAY` | 显示对话框 | 渲染并等待点击 |
| `OGAL_EVT_CHOICE` | 显示选项 | 渲染并等待点击选项 |
| `OGAL_EVT_END` | 剧本结束 | 通常黑屏/退出 |

## 剧本系统

`gal_script.cpp` 将剧本编译为字节码（`Instruction` 数组 + label 表），运行时由 `ScriptVM::step` 逐条执行：

- 表现类指令（scene/show/play…）直接回调宿主 `IHost`，宿主（引擎主体）加载纹理/播放音频并更新舞台状态
- 阻塞类指令（SAY/CHOICE）挂起 VM，把事件透传给平台
- 控制类指令（JUMP/CALL/IF/SET）在 VM 内完成，变量为整型，存于 VM

编译期完成缩进解析、if/elif/else 跳转回填、menu 选项表收集与 label 解析，运行时无文本解析开销。

## 渲染

- **纹理**：stb_image 解码 PNG/JPG → RGBA8888 → `create_texture`（内部翻转为 GL 自下而上）
- **背景**：cover 铺满并居中裁剪；**立绘**：高 85% 屏幕、底部对齐、水平三档位置（left 0.25 / center 0.5 / right 0.75）
- **文本**：stb_truetype 按需光栅化进 1024×1024 图集，按 (codepoint, px) 缓存；支持 UTF-8 / 中文
- **双后端**：桌面走 GL 1.1 固定管线（`opengl32.lib`，免函数加载）；GLES2 走两个着色器，EGL 上下文由渲染器在 `init(native_window)` 中创建

## 音频

`opensl_audio.cpp` 同时服务 Android 与 HarmonyOS（两者 NDK 都内置 OpenSL ES）。内置 RIFF/WAVE PCM16 解码，BGM 一个可循环 Player，音效一个一次性 Player。**数据保活**：OpenSL BufferQueue 不拷贝数据，后端将 PCM 存入成员向量后再入队。OGG/MP3 只需替换 `decode_wav()`。

## 已知限制 / 路线图

- 立绘淡入淡出、文本打字机效果、存档系统（变量已集中在 VM 中，便于序列化）
- `.galpack` 压缩包运行时读取（当前 zip 仅作分发格式）
- 桌面音频后端（WASAPI）
- 多剧本 `include`、宏与内联表达式

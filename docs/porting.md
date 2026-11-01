# 平台移植指南

OpenGal 的核心目标是：**移植一个新平台时，一行引擎核心代码都不用改**。

## 你需要实现什么

三个 vtable（定义见 `engine/include/opengal.h`）：

### 1. ogal_platform

| 成员 | 职责 | 提示 |
|---|---|---|
| `read_file` | 按相对路径读取整个文件，malloc 返回 | Win32 用 `fopen`；Android 用 `AAssetManager`；HarmonyOS 用 `OH_ResourceManager_OpenRawFile`；iOS 用 `[NSBundle mainBundle]` 路径拼接 |
| `log` | 日志 | `OutputDebugString` / `__android_log_print` / `OH_LOG_INFO` |
| `time_ms` | 毫秒单调时钟 | `GetTickCount64` / `clock_gettime(CLOCK_MONOTONIC)` |

### 2. 渲染器

最简单的方式是**直接复用内置 GL 渲染器**：

```c
ogal_engine* eng = ogal_create(&pf, "gl", "opensl");
ogal_renderer* r = ogal_engine_get_renderer(eng);
r->init(r, native_window);   // HWND / ANativeWindow* / OHNativeWindow*
ogal_resize(eng, w, h);
```

- 编译核心时给 `gl_renderer.cpp` 定义 `OGAL_GLES2` 即得到 EGL + GLES2 后端（Android / HarmonyOS / iOS / Linux 嵌入式都适用）；不定义则得到 WGL + GL1.1 后端（Windows）。
- 需要 Vulkan / Metal / D3D 时，自实现 `ogal_renderer` 的 10 个函数即可，引擎布局层（对话框、选项）完全复用。

### 3. ogal_audio

- `"opensl"`：OpenSL ES 后端（Android / HarmonyOS 直接可用，链接 `OpenSLES`）
- `"null"`：静音后端（桌面调试、或音频待接入时）
- 自实现：实现 4 个函数即可；注意 `play_*` 是同步调用，**后端需自行拷贝**所需数据（引擎随后 `free`）

## 主循环模板

```c
ogal_load(eng, "script/main.gal");
ogal_event ev;
ogal_event_type t = ogal_step(eng, &ev);
while (running) {
    /* 1. 平台事件循环; 点击时: ogal_on_tap(eng, x, y); */
    /* 2. 绘制: */
    ogal_render(eng);
    /* 3. 调度: */
    if (t == OGAL_EVT_NONE)  sleep(16);      // wait/过渡动画中
    else                     wait_input();   // 对话/选项: 休眠直到点击
    /* 4. 推进: */
    t = ogal_step(eng, &ev);
    if (t == OGAL_EVT_END) break;            // 或继续黑屏
}
```

要点：

- `ogal_on_tap` 之后**必须**再次调用 `ogal_step` 才会继续推进剧本
- `OGAL_EVT_NONE` 期间也请保持 `ogal_render` 调用（淡入淡出依赖它推进）
- 多线程调用引擎请加锁（参考 `platform/harmonyos` 的 mutex 用法）

## 参考实现

| 平台 | 入口 | 关键 API |
|---|---|---|
| Windows | `platform/win32/main.cpp` | Win32 窗口、WGL |
| Android | `platform/android/app/src/main/cpp/android_main.cpp` | NativeActivity、`android_native_app_glue`、AAssetManager |
| HarmonyOS | `platform/harmonyos/entry/src/main/cpp/napi_init.cpp` | XComponent、NAPI、ResourceManager |

## 加入构建

- CMake 类平台（Linux、iOS 交叉等）：仿照根 `CMakeLists.txt` 把核心源文件加进你的目标，定义 `OGAL_GLES2`，链接 EGL/GLESv2。
- 资源打包：把游戏目录放进平台资源机制（apk assets / rawfile / 文件系统目录），保证 `read_file` 能按相对路径取到即可。

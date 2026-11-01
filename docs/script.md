# 剧本语言参考

剧本是 UTF-8 纯文本（`.gal`），入口为 `script/main.gal`。语法受 Ren'Py 启发，**用缩进表示块**，`#` 到行尾为注释。

## 语句一览

| 语句 | 说明 |
|---|---|
| `label 名称` | 定义跳转锚点 |
| `scene 路径 [fade]` | 切换背景（`fade` 表示黑场淡入），并清空立绘 |
| `show 路径 [at left\|center\|right] [fade]` | 显示立绘；同名路径再次 show 视为换图 |
| `hide 路径` | 移除立绘 |
| `说话人 "文本"` | 对话；说话人可用中文 |
| `"文本"` | 旁白（无说话人） |
| `menu:` 块 | 选项菜单，见下 |
| `play music 路径 [loop]` | 播放 BGM（`loop` 循环） |
| `play sound 路径` | 播放音效 |
| `stop music` | 停止 BGM |
| `set 变量 = 数字或变量` | 赋值（整型变量，无需声明，默认 0） |
| `if 变量 == 数字:` 块 | 条件分支，支持 `elif` / `else` |
| `wait 毫秒` | 等待 |
| `jump 标签` / `call 标签` / `return` | 跳转 / 子流程调用 / 返回 |

比较运算符：`== != < <= > >=`。

## 选项菜单

`menu:` 之后是缩进的选项列表，每个选项两种形态：

```gal
menu:
    "带内联剧本的选项":
        scene bg/a.png
        苍 "这是选项 A 的剧本。"
    "跳转到 label 的选项" -> somewhere
    "再一个内联选项":
        jump end
```

- 内联块执行完后会自然落到菜单之后继续
- 带 `->` 的选项等价于 `jump 标签`

## 字符串

双引号包裹，支持转义：`\"` `\\` `\n` `\t`。文本按 UTF-8 处理，自动换行以对话框宽度为准。

## 资源路径

语句中的路径都是**相对于游戏目录**的：

```gal
scene bg/sky.png            # -> <游戏目录>/bg/sky.png
show char/heroine.png
play music bgm/theme.wav loop
play sound se/click.wav
```

图片用 stb_image 解码（PNG/JPEG/…），音频目前支持 WAV(PCM16)。字体放到 `font/default.ttf`。

## 完整示例

```gal
label start
scene bg/classroom.png fade
play music bgm/soft.wav loop
show char/girl.png at center

girl "早上好。"
menu:
    "回应她":
        girl "你回应了。"
    "装作没看见" -> ignore
label ignore
set met = 1
if met == 1:
    girl "if 分支: met == 1。"
else:
    girl "else 分支。"
"—— 完 ——"
```

## 校验

```bash
python tools/pack.py check <游戏目录>
```

会检查剧本引用的每个资源文件是否存在。

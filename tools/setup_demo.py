#!/usr/bin/env python3
"""生成演示所需的全部二进制资源(游戏图片/音频 + 字体 + 应用图标)。

仓库不保存二进制资源; 首次运行或资源缺失时执行:

  python tools/setup_demo.py

- 游戏图片/音频: 调用 games/demo/assets_gen.py 生成占位资源
- 字体: 用 fontTools 从系统字体(黑体/雅黑/Noto 等)中子集化出示例用字体
- 图标: 生成 HarmonyOS 模块所需的应用图标

换成自己的游戏时, 只需保留目录结构替换资源即可, 无需再运行本脚本。
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FONT_CANDIDATES = [
    "C:/Windows/Fonts/simhei.ttf",       # 黑体
    "C:/Windows/Fonts/msyh.ttc",         # 微软雅黑
    "C:/Windows/Fonts/Deng.ttf",         # 等线
    "/System/Library/Fonts/PingFang.ttc",  # macOS
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",  # Linux
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
]


def make_icon() -> None:
    from PIL import Image, ImageDraw
    path = os.path.join(ROOT, "platform/harmonyos/entry/src/main/resources/base/media/icon.png")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([4, 4, 124, 124], radius=24, fill=(88, 28, 135, 255))
    d.polygon([(44, 88), (64, 40), (84, 88)], fill=(255, 255, 255, 255))
    d.rectangle([52, 88, 76, 96], fill=(255, 255, 255, 255))
    img.save(path)
    print("[ok] 应用图标 ->", path)


def make_font() -> None:
    src = next((p for p in FONT_CANDIDATES if os.path.exists(p)), None)
    if not src:
        print("[跳过] 未找到系统中文字体, 可手动放置 TTF 到 games/demo/font/default.ttf")
        return
    try:
        import fontTools  # noqa: F401
    except ImportError:
        print("[跳过] 未安装 fontTools (pip install fonttools), 可手动放置字体")
        return
    out = os.path.join(ROOT, "games/demo/font/default.ttf")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    script = os.path.join(ROOT, "games/demo/script/main.gal")
    chars = open(script, encoding="utf-8").read()
    tmp = os.path.join(ROOT, "_font_chars.txt")
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(chars + "\n".join("，。！？、：；“”‘’（）《》—…▼0123456789"))
    r = subprocess.run(
        [sys.executable, "-m", "fontTools.subset", src,
         f"--output-file={out}", f"--text-file={tmp}",
         "--layout-features=*", "--name-IDs=*", "--name-legacy",
         "--name-languages=*"],
        capture_output=True, text=True)
    os.remove(tmp)
    if r.returncode == 0 and os.path.exists(out):
        print(f"[ok] 字体子集 -> {out} ({os.path.getsize(out) // 1024} KB, 来源 {os.path.basename(src)})")
    else:
        print("[跳过] 字体子集化失败:", (r.stderr or r.stdout)[-300:])


def main() -> int:
    gen = os.path.join(ROOT, "games/demo/assets_gen.py")
    subprocess.run([sys.executable, gen], check=True)
    make_font()
    make_icon()
    print("\n示例资源就绪。运行游戏前可用 python tools/pack.py check games/demo 校验。")
    return 0


if __name__ == "__main__":
    sys.exit(main())

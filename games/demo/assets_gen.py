#!/usr/bin/env python3
"""生成 OpenGal 示例游戏的占位资源(渐变背景 / 立绘剪影 / 合成音频)。

真实项目中替换为美术资源即可, 目录结构保持不变:
  bg/     背景图 (PNG/JPG)
  char/   立绘    (PNG, 建议透明背景)
  bgm/    音乐    (WAV PCM16)
  se/     音效    (WAV PCM16)
  font/   default.ttf 字体(可选, 缺失时文本不绘制)
"""
import math
import os
import struct
import sys
import wave

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))


def make_bg(path: str, w: int = 1280, h: int = 720) -> None:
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        t = y / h
        r = int(120 + 100 * t)
        g = int(170 + 40 * t)
        b = int(230 + 20 * (1 - t))
        for x in range(w):
            px[x, y] = (r, g, b)
    d = ImageDraw.Draw(img)
    d.ellipse([w * 0.72, h * 0.12, w * 0.9, h * 0.38], fill=(255, 244, 200))   # 太阳
    d.ellipse([w * 0.1, h * 0.65, w * 0.5, h * 1.05], fill=(110, 170, 120))    # 远山
    d.ellipse([w * 0.4, h * 0.7, w * 0.95, h * 1.1], fill=(90, 150, 105))      # 近山
    img.save(path)


def make_char(path: str, w: int = 540, h: int = 1080) -> None:
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx = w // 2
    skin, hair, cloth = (255, 224, 200, 255), (120, 80, 160, 255), (220, 120, 150, 255)
    d.ellipse([cx - 150, h * 0.52, cx + 150, h * 0.98], fill=cloth)             # 身体
    d.ellipse([cx - 105, h * 0.30, cx + 105, h * 0.55], fill=skin)               # 脸
    d.pieslice([cx - 130, h * 0.24, cx + 130, h * 0.50], 180, 360, fill=hair)    # 刘海
    d.ellipse([cx - 135, h * 0.27, cx - 65, h * 0.42], fill=hair)                # 侧发
    d.ellipse([cx + 65, h * 0.27, cx + 135, h * 0.42], fill=hair)
    d.ellipse([cx - 60, h * 0.40, cx - 30, h * 0.45], fill=(60, 40, 80, 255))    # 眼
    d.ellipse([cx + 30, h * 0.40, cx + 60, h * 0.45], fill=(60, 40, 80, 255))
    d.arc([cx - 25, h * 0.45, cx + 25, h * 0.51], 10, 170, fill=(200, 90, 110, 255), width=6)
    img.save(path)


def _wav(path: str, freq: float, secs: float, vol: float = 0.4, rate: int = 44100) -> None:
    n = int(rate * secs)
    with wave.open(path, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(rate)
        frames = bytearray()
        for i in range(n):
            env = math.sin(math.pi * i / n) ** 2
            v = int(32767 * vol * env * math.sin(2 * math.pi * freq * i / rate))
            frames += struct.pack("<h", v)
        f.writeframes(bytes(frames))


def main() -> None:
    make_bg(os.path.join(HERE, "bg", "sky.png"))
    make_char(os.path.join(HERE, "char", "heroine.png"))
    _wav(os.path.join(HERE, "bgm", "theme.wav"), 261.6, 2.0, 0.3)
    _wav(os.path.join(HERE, "se", "click.wav"), 880.0, 0.15, 0.4)
    print("示例资源已生成:", HERE)


if __name__ == "__main__":
    sys.exit(main())

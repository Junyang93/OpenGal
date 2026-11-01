#!/usr/bin/env python3
"""OpenGal 游戏资源工具

用法:
  python tools/pack.py check <游戏目录>          校验剧本引用的资源是否齐全
  python tools/pack.py zip   <游戏目录> <输出.galpack>   打包为 zip 格式
  python tools/pack.py ohos  <游戏目录>          同步到 HarmonyOS rawfile 目录
  python tools/pack.py android <游戏目录>        同步到 Android assets 目录
"""
import os
import re
import shutil
import sys
import zipfile

RE_REF = re.compile(r'^\s*(scene|show|hide|play)\s+(?:(music|sound)\s+)?(\S+)', re.M)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def game_files(game_dir: str):
    for base, _dirs, files in os.walk(game_dir):
        for f in files:
            if f.endswith((".gal", ".py")):
                continue
            full = os.path.join(base, f)
            yield full, os.path.relpath(full, game_dir).replace(os.sep, "/")


def cmd_check(game_dir: str) -> int:
    script = os.path.join(game_dir, "script", "main.gal")
    if not os.path.exists(script):
        print(f"[失败] 找不到剧本: {script}")
        return 1
    src = open(script, encoding="utf-8").read()
    missing = []
    for m in RE_REF.finditer(src):
        path = m.group(3)
        if not os.path.exists(os.path.join(game_dir, path)):
            missing.append(path)
    if missing:
        print("[失败] 缺失资源:")
        for p in missing:
            print("  -", p)
        return 1
    n = sum(1 for _ in game_files(game_dir))
    print(f"[通过] 剧本 OK, 资源 {n} 个")
    return 0


def cmd_zip(game_dir: str, out: str) -> int:
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for full, rel in game_files(game_dir):
            z.write(full, rel)
    print(f"[完成] {out} ({os.path.getsize(out) // 1024} KB)")
    return 0


def sync(game_dir: str, target: str) -> int:
    if os.path.exists(target):
        shutil.rmtree(target)
    os.makedirs(target)
    count = 0
    for full, rel in game_files(game_dir):
        dst = os.path.join(target, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(full, dst)
        count += 1
    print(f"[完成] 已同步 {count} 个文件到 {target}")
    return 0


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    op = sys.argv[1]
    game_dir = os.path.abspath(sys.argv[2])
    if op == "check":
        return cmd_check(game_dir)
    if op == "zip":
        return cmd_zip(game_dir, sys.argv[3])
    if op == "ohos":
        t = os.path.join(ROOT, "platform/harmonyos/entry/src/main/resources/rawfile")
        return sync(game_dir, t)
    if op == "android":
        t = os.path.join(ROOT, "platform/android/app/src/main/assets")
        return sync(game_dir, t)
    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main())

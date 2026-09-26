"""
PlatformIO pre-script: читає .env у корені проєкту і генерує заголовок
env/env_config.h, де кожен рядок KEY=VALUE стає

    #define KEY "VALUE"

Чому заголовок, а не BUILD_FLAGS (-DKEY=...): у PlatformIO + ESP-IDF
build_flags отримують лише файли з src/, а не окремі компоненти (task/...,
components/...). Заголовок же можна підключити з будь-якого компонента.

env/env_config.h містить секрети (API токен) - він у .gitignore, як і .env.
Файл перезаписується лише тоді, коли його вміст змінився, щоб не
перекомпільовувати проєкт на кожному запуску.
"""
import os
import re

Import("env")

project_dir = env.get("PROJECT_DIR")
env_file_path = os.path.join(project_dir, ".env")
header_path = os.path.join(project_dir, "env", "env_config.h")


def c_string(value):
    """Екранує значення як літерал рядка C."""
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


defines = []
if os.path.exists(env_file_path):
    print(f"--- [ENV] Loading variables from {env_file_path} ---")
    with open(env_file_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            # Пропускаємо коментарі й порожні рядки
            if not line or line.startswith("#"):
                continue

            match = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)\s*=(.*)$", line)
            if not match:
                print(f"    [ENV] Skipped line (expected KEY=VALUE): {line}")
                continue

            key = match.group(1)
            val = match.group(2).strip()
            # Прибрати лапки навколо значення, якщо вони є у файлі
            if len(val) >= 2 and val[0] == val[-1] and val[0] in ('"', "'"):
                val = val[1:-1]

            defines.append(f"#define {key} {c_string(val)}")
            # Значення не друкуємо - там може бути токен
            print(f"    Added macro: {key}")
else:
    print(f"--- [ENV] Warning: .env file not found at {env_file_path} ---")

content = (
    "/* Згенеровано env/env.py з файлу .env - не редагувати вручну. */\n"
    "#pragma once\n\n"
    + "\n".join(defines)
    + ("\n" if defines else "")
)

old = None
if os.path.exists(header_path):
    with open(header_path, "r", encoding="utf-8") as f:
        old = f.read()
if old != content:
    with open(header_path, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"--- [ENV] Wrote {header_path} ---")

#!/usr/bin/env python3
"""Tkinter editor for Tamagotchi SaveData v3 binaries (/backup/save_*.bin)."""

from __future__ import annotations

import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from typing import Dict, Optional

from save_format import SAVE_SIZE, SaveData, load_path, save_path


def main() -> None:
    root = tk.Tk()
    root.title("Tamagotchi Save Editor")
    root.geometry("560x720")
    root.minsize(500, 600)

    path_var = tk.StringVar(value="(нет файла)")
    data = SaveData()
    vars_map: Dict[str, tk.Variable] = {}
    current_path: Optional[str] = None

    bar = ttk.Frame(root, padding=6)
    bar.pack(fill=tk.X)
    ttk.Label(bar, textvariable=path_var).pack(side=tk.RIGHT)

    canvas = tk.Canvas(root, highlightthickness=0)
    scroll = ttk.Scrollbar(root, orient=tk.VERTICAL, command=canvas.yview)
    canvas.configure(yscrollcommand=scroll.set)
    scroll.pack(side=tk.RIGHT, fill=tk.Y)
    canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
    body = ttk.Frame(canvas, padding=8)
    win = canvas.create_window((0, 0), window=body, anchor=tk.NW)

    def on_cfg(_event=None) -> None:
        canvas.configure(scrollregion=canvas.bbox("all"))
        canvas.itemconfigure(win, width=max(canvas.winfo_width(), 1))

    body.bind("<Configure>", on_cfg)
    canvas.bind("<Configure>", on_cfg)

    def section(title: str) -> None:
        ttk.Separator(body).pack(fill=tk.X, pady=(10, 4))
        ttk.Label(body, text=title, font=("", 10, "bold")).pack(anchor=tk.W)

    def int_row(key: str, label: str, lo: int, hi: int) -> None:
        row = ttk.Frame(body)
        row.pack(fill=tk.X, pady=1)
        ttk.Label(row, text=label, width=28).pack(side=tk.LEFT)
        var = tk.IntVar(value=0)
        vars_map[key] = var
        top = hi if hi <= 10_000_000 else 10_000_000
        ttk.Spinbox(row, from_=lo, to=top, textvariable=var, width=14).pack(side=tk.RIGHT)

    def food_row() -> None:
        section("Еда food[0..7]")
        row = ttk.Frame(body)
        row.pack(fill=tk.X, pady=2)
        for i in range(8):
            cell = ttk.Frame(row)
            cell.pack(side=tk.LEFT, padx=3)
            ttk.Label(cell, text=str(i)).pack()
            var = tk.IntVar(value=0)
            vars_map[f"food{i}"] = var
            ttk.Spinbox(cell, from_=0, to=9, textvariable=var, width=4).pack()

    def pet_block(idx: int, name: str) -> None:
        section(f"Питомец {idx}: {name}")
        for field, label, lo, hi in (
            ("hunger", "Голод", 0, 100),
            ("happy", "Счастье", 0, 100),
            ("health", "Здоровье", 0, 100),
            ("energy", "Энергия", 0, 100),
            ("clean", "Чистота", 0, 100),
            ("level", "Уровень", 1, 99),
            ("xp", "XP", 0, 999999),
            ("age_sec", "Возраст (сек)", 0, 2_000_000_000),
            ("head", "head id", 0, 255),
            ("outfit", "outfit id", 0, 255),
            ("extra", "extra id", 0, 255),
            ("sleeping", "Спит (0/1)", 0, 1),
        ):
            int_row(f"pet{idx}_{field}", label, lo, hi)

    section("Общее")
    int_row("coins", "Монеты", 0, 999999)
    int_row("active", "Активный (0/1)", 0, 1)
    int_row("started", "Игра начата (0/1)", 0, 1)
    int_row("clothes", "Маска одежды", 0, 0xFFFFFFFF)
    int_row("last_unix", "last_unix", 0, 0xFFFFFFFF)
    int_row("theme", "Тема 0/1", 0, 1)
    int_row("dim_idx", "dim_idx", 0, 5)
    int_row("off_idx", "off_idx", 0, 4)
    int_row("bl_idx", "bl_idx", 0, 3)
    int_row("led_flash", "led_flash", 0, 1)
    int_row("show_fps", "show_fps", 0, 1)
    int_row("trans_idx", "Переход 0–2", 0, 2)
    int_row("pace_idx", "Темп нужд 0–2", 0, 2)
    int_row("btn0", "Кнопка BOOT 0–10", 0, 10)
    int_row("btn1", "Кнопка GPIO 12 0–10", 0, 10)
    int_row("btn2", "Кнопка GPIO 16 0–10", 0, 10)
    food_row()
    pet_block(0, "Мими")
    pet_block(1, "Нори")

    def push() -> None:
        vars_map["coins"].set(data.coins)
        vars_map["active"].set(data.active)
        vars_map["started"].set(data.started)
        vars_map["clothes"].set(data.clothes)
        vars_map["last_unix"].set(data.last_unix)
        vars_map["theme"].set(data.theme)
        vars_map["dim_idx"].set(data.dim_idx)
        vars_map["off_idx"].set(data.off_idx)
        vars_map["bl_idx"].set(data.bl_idx)
        vars_map["led_flash"].set(data.led_flash)
        vars_map["show_fps"].set(data.show_fps)
        vars_map["trans_idx"].set(data.trans_idx)
        vars_map["pace_idx"].set(data.pace_idx)
        for i in range(3):
            vars_map[f"btn{i}"].set(data.btn[i] if i < len(data.btn) else 0)
        for i in range(8):
            vars_map[f"food{i}"].set(data.food[i] if i < len(data.food) else 0)
        for pi in (0, 1):
            p = data.pet[pi]
            for field in (
                "hunger",
                "happy",
                "health",
                "energy",
                "clean",
                "level",
                "xp",
                "age_sec",
                "head",
                "outfit",
                "extra",
                "sleeping",
            ):
                vars_map[f"pet{pi}_{field}"].set(getattr(p, field))

    def pull() -> None:
        nonlocal data
        data.coins = int(vars_map["coins"].get())
        data.active = int(vars_map["active"].get()) & 1
        data.started = int(vars_map["started"].get()) & 1
        data.clothes = int(vars_map["clothes"].get()) & 0xFFFFFFFF
        data.last_unix = int(vars_map["last_unix"].get()) & 0xFFFFFFFF
        data.theme = int(vars_map["theme"].get()) & 1
        data.dim_idx = int(vars_map["dim_idx"].get())
        data.off_idx = int(vars_map["off_idx"].get())
        data.bl_idx = int(vars_map["bl_idx"].get())
        data.led_flash = int(vars_map["led_flash"].get()) & 1
        data.show_fps = int(vars_map["show_fps"].get()) & 1
        data.trans_idx = int(vars_map["trans_idx"].get())
        data.pace_idx = int(vars_map["pace_idx"].get())
        data.btn = [int(vars_map[f"btn{i}"].get()) & 0xFF for i in range(3)]
        data.food = [int(vars_map[f"food{i}"].get()) for i in range(8)]
        for pi in (0, 1):
            p = data.pet[pi]
            for field in (
                "hunger",
                "happy",
                "health",
                "energy",
                "clean",
                "level",
                "xp",
                "age_sec",
                "head",
                "outfit",
                "extra",
                "sleeping",
            ):
                setattr(p, field, int(vars_map[f"pet{pi}_{field}"].get()))

    def open_file() -> None:
        nonlocal data, current_path
        path = filedialog.askopenfilename(
            title="Open save",
            filetypes=[("Save bin", "*.bin"), ("All", "*.*")],
        )
        if not path:
            return
        loaded, err = load_path(path)
        if err and not messagebox.askyesno("Warning", f"{err}\n\nОткрыть всё равно?"):
            return
        data = loaded
        current_path = path
        path_var.set(path)
        push()

    def do_save(path: str) -> None:
        nonlocal current_path
        pull()
        try:
            save_path(path, data)
        except OSError as exc:
            messagebox.showerror("Error", str(exc))
            return
        current_path = path
        path_var.set(path)
        messagebox.showinfo("Saved", f"Записано {SAVE_SIZE} байт\nCRC 0x{data.crc:08X}")

    def save_file() -> None:
        if not current_path:
            save_as()
            return
        do_save(current_path)

    def save_as() -> None:
        path = filedialog.asksaveasfilename(
            title="Save as",
            defaultextension=".bin",
            filetypes=[("Save bin", "*.bin"), ("All", "*.*")],
        )
        if path:
            do_save(path)

    ttk.Button(bar, text="Open…", command=open_file).pack(side=tk.LEFT, padx=2)
    ttk.Button(bar, text="Save", command=save_file).pack(side=tk.LEFT, padx=2)
    ttk.Button(bar, text="Save As…", command=save_as).pack(side=tk.LEFT, padx=2)

    push()
    root.mainloop()


if __name__ == "__main__":
    main()

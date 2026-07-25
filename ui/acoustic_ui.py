"""Lightweight single-window UI for Acoustic File Transfer."""

from __future__ import annotations

import argparse
import codecs
import os
import queue
import signal
import subprocess
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk


ROOT = Path(__file__).resolve().parents[1]


def find_binary(explicit: str | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))
    if os.environ.get("ACOUSTIC_TRANSFER_BIN"):
        candidates.append(Path(os.environ["ACOUSTIC_TRANSFER_BIN"]))
    executable = "acoustic-transfer.exe" if os.name == "nt" else "acoustic-transfer"
    candidates.extend(
        [
            ROOT / "build" / executable,
            ROOT / "build" / "Release" / executable,
            ROOT / "build" / "Debug" / executable,
        ]
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise FileNotFoundError(
        "Сначала соберите acoustic-transfer. Ожидаемый путь: build/acoustic-transfer"
    )


class AcousticUi(tk.Tk):
    def __init__(self, binary: Path) -> None:
        super().__init__()
        self.binary = binary
        self.process: subprocess.Popen[bytes] | None = None
        self.messages: queue.Queue[tuple[str, object]] = queue.Queue()
        self.title("Acoustic File Transfer")
        self.geometry("940x650")
        self.minsize(780, 560)
        self.configure(bg="#0b1118")
        self._configure_style()
        self._build_page()
        self.after(70, self._drain_messages)

    def _configure_style(self) -> None:
        style = ttk.Style(self)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure("Page.TFrame", background="#0b1118")
        style.configure("Panel.TFrame", background="#111b26")
        style.configure("Title.TLabel", background="#0b1118", foreground="#f1f7fb", font=("Segoe UI", 22, "bold"))
        style.configure("Muted.TLabel", background="#0b1118", foreground="#8ca0b3", font=("Segoe UI", 10))
        style.configure("Panel.TLabel", background="#111b26", foreground="#b7c7d6", font=("Segoe UI", 10))
        style.configure("Accent.TButton", background="#2bd4a8", foreground="#06120f", padding=(16, 11), font=("Segoe UI", 10, "bold"))
        style.map("Accent.TButton", background=[("active", "#5be3be"), ("disabled", "#39564f")])
        style.configure("Action.TButton", background="#1a2a38", foreground="#e7f0f6", padding=(14, 10), font=("Segoe UI", 10))
        style.map("Action.TButton", background=[("active", "#243b4e"), ("disabled", "#18232d")])
        style.configure("Danger.TButton", background="#442631", foreground="#ffbacb", padding=(14, 10))
        style.configure("TCombobox", fieldbackground="#172431", background="#172431", foreground="#f1f7fb", padding=6)
        style.configure("TEntry", fieldbackground="#172431", foreground="#f1f7fb", padding=7)

    def _build_page(self) -> None:
        page = ttk.Frame(self, style="Page.TFrame", padding=24)
        page.pack(fill="both", expand=True)
        page.columnconfigure(0, weight=1)
        page.rowconfigure(3, weight=1)

        heading = ttk.Frame(page, style="Page.TFrame")
        heading.grid(row=0, column=0, sticky="ew")
        heading.columnconfigure(0, weight=1)
        ttk.Label(heading, text="Acoustic File Transfer", style="Title.TLabel").grid(row=0, column=0, sticky="w")
        ttk.Label(heading, text="Файлы через звук · локально · с проверкой целостности", style="Muted.TLabel").grid(row=1, column=0, sticky="w", pady=(4, 0))
        self.status = ttk.Label(heading, text="ГОТОВО", style="Muted.TLabel")
        self.status.grid(row=0, column=1, rowspan=2, sticky="e")

        settings = ttk.Frame(page, style="Panel.TFrame", padding=16)
        settings.grid(row=1, column=0, sticky="ew", pady=(20, 12))
        ttk.Label(settings, text="Профиль", style="Panel.TLabel").grid(row=0, column=0, sticky="w")
        self.profile = tk.StringVar(value="balanced")
        ttk.Combobox(settings, textvariable=self.profile, values=("fast", "balanced", "robust"), state="readonly", width=14).grid(row=1, column=0, padx=(0, 18), pady=(5, 0))
        ttk.Label(settings, text="Запись, сек", style="Panel.TLabel").grid(row=0, column=1, sticky="w")
        self.seconds = tk.StringVar(value="30")
        ttk.Entry(settings, textvariable=self.seconds, width=10).grid(row=1, column=1, padx=(0, 18), pady=(5, 0))
        ttk.Label(settings, text="Усиление (0 = auto)", style="Panel.TLabel").grid(row=0, column=2, sticky="w")
        self.gain = tk.StringVar(value="0")
        ttk.Entry(settings, textvariable=self.gain, width=14).grid(row=1, column=2, pady=(5, 0))

        actions = ttk.Frame(page, style="Page.TFrame")
        actions.grid(row=2, column=0, sticky="ew", pady=(0, 12))
        for column in range(4):
            actions.columnconfigure(column, weight=1)
        self.action_buttons: list[ttk.Button] = []
        buttons = [
            ("Передать файл", self.send_file, "Accent.TButton"),
            ("Принять файл", self.receive_file, "Accent.TButton"),
            ("Оценить", self.estimate_file, "Action.TButton"),
            ("Калибровать", self.calibrate, "Action.TButton"),
            ("Создать WAV", self.encode_file, "Action.TButton"),
            ("Декодировать WAV", self.decode_file, "Action.TButton"),
            ("Проверить аудио", lambda: self.run_command(["check-audio"]), "Action.TButton"),
            ("Самопроверка", lambda: self.run_command(["self-test", *self.profile_args()]), "Action.TButton"),
            ("Эффективность", lambda: self.run_command(["benchmark", "256", *self.profile_args()]), "Action.TButton"),
        ]
        for index, (label, callback, style) in enumerate(buttons):
            button = ttk.Button(actions, text=label, command=callback, style=style)
            button.grid(row=index // 4, column=index % 4, sticky="ew", padx=4, pady=4)
            self.action_buttons.append(button)

        console_panel = ttk.Frame(page, style="Panel.TFrame", padding=14)
        console_panel.grid(row=3, column=0, sticky="nsew")
        console_panel.columnconfigure(0, weight=1)
        console_panel.rowconfigure(1, weight=1)
        toolbar = ttk.Frame(console_panel, style="Panel.TFrame")
        toolbar.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        toolbar.columnconfigure(0, weight=1)
        ttk.Label(toolbar, text="Журнал операции", style="Panel.TLabel").grid(row=0, column=0, sticky="w")
        self.cancel_button = ttk.Button(toolbar, text="Остановить", command=self.cancel, style="Danger.TButton", state="disabled")
        self.cancel_button.grid(row=0, column=1, padx=(8, 0))
        ttk.Button(toolbar, text="Очистить", command=lambda: self.console.delete("1.0", "end"), style="Action.TButton").grid(row=0, column=2, padx=(8, 0))
        self.console = tk.Text(console_panel, bg="#081018", fg="#c8d8e5", insertbackground="#ffffff", relief="flat", padx=12, pady=10, font=("Cascadia Mono", 10), wrap="word")
        self.console.grid(row=1, column=0, sticky="nsew")
        scrollbar = ttk.Scrollbar(console_panel, orient="vertical", command=self.console.yview)
        scrollbar.grid(row=1, column=1, sticky="ns")
        self.console.configure(yscrollcommand=scrollbar.set)
        self._append(f"Исполняемый файл: {self.binary}\nВыберите действие.\n")

    def profile_args(self) -> list[str]:
        return ["--profile", self.profile.get()]

    def _choose_input(self, title: str, filetypes: list[tuple[str, str]] | None = None) -> str:
        return filedialog.askopenfilename(title=title, filetypes=filetypes or [("Все файлы", "*.*")])

    def send_file(self) -> None:
        source = self._choose_input("Выберите файл для передачи")
        if source:
            self.run_command(["send", source, *self.profile_args()])

    def receive_file(self) -> None:
        destination = filedialog.askdirectory(title="Папка для принятых файлов")
        if destination:
            self.run_command(["receive", destination, "--seconds", self.seconds.get(), "--gain", self.gain.get(), *self.profile_args()])

    def estimate_file(self) -> None:
        source = self._choose_input("Выберите файл для оценки")
        if source:
            self.run_command(["estimate", source, *self.profile_args()])

    def calibrate(self) -> None:
        self.run_command(["calibrate", *self.profile_args()])

    def encode_file(self) -> None:
        source = self._choose_input("Выберите исходный файл")
        if not source:
            return
        destination = filedialog.asksaveasfilename(title="Сохранить звуковой кадр", defaultextension=".wav", filetypes=[("WAV", "*.wav")])
        if destination:
            self.run_command(["encode", source, destination, *self.profile_args(), "--force"])

    def decode_file(self) -> None:
        source = self._choose_input("Выберите WAV", [("WAV", "*.wav"), ("Все файлы", "*.*")])
        if not source:
            return
        destination = filedialog.asksaveasfilename(title="Сохранить восстановленный файл")
        if destination:
            self.run_command(["decode", source, destination, "--gain", self.gain.get(), *self.profile_args(), "--force"])

    def run_command(self, arguments: list[str]) -> None:
        if self.process is not None:
            messagebox.showinfo("Операция выполняется", "Дождитесь завершения или нажмите «Остановить».")
            return
        command = [str(self.binary), *arguments]
        self._append("\n$ " + " ".join(f'"{part}"' if " " in part else part for part in arguments) + "\n")
        self._set_running(True)

        def worker() -> None:
            try:
                startup = None
                creationflags = 0
                if os.name == "nt":
                    startup = subprocess.STARTUPINFO()
                    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                    creationflags = subprocess.CREATE_NO_WINDOW
                self.process = subprocess.Popen(
                    command,
                    cwd=ROOT,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=False,
                    bufsize=-1,
                    startupinfo=startup,
                    creationflags=creationflags,
                    start_new_session=os.name != "nt",
                )
                assert self.process.stdout is not None
                decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
                while True:
                    chunk = self.process.stdout.read1(4096)
                    if not chunk:
                        break
                    text = decoder.decode(chunk)
                    if text:
                        self.messages.put(("output", text))
                tail = decoder.decode(b"", final=True)
                if tail:
                    self.messages.put(("output", tail))
                code = self.process.wait()
                self.messages.put(("done", code))
            except Exception as error:  # UI boundary: report unexpected launch failures.
                self.messages.put(("error", str(error)))

        threading.Thread(target=worker, daemon=True).start()

    def cancel(self) -> None:
        if self.process is not None and self.process.poll() is None:
            if os.name == "nt":
                self.process.terminate()
            else:
                os.killpg(self.process.pid, signal.SIGTERM)
            self._append("\nОстановка запрошена…\n")

    def _set_running(self, running: bool) -> None:
        self.status.configure(text="ВЫПОЛНЯЕТСЯ" if running else "ГОТОВО")
        for button in self.action_buttons:
            button.configure(state="disabled" if running else "normal")
        self.cancel_button.configure(state="normal" if running else "disabled")

    def _append(self, text: str) -> None:
        # Progress uses carriage returns. Keeping only the latest textual state
        # avoids filling the log with hundreds of almost identical lines.
        text = text.replace("\r", "\n")
        self.console.insert("end", text)
        self.console.see("end")

    def _drain_messages(self) -> None:
        output: list[str] = []
        try:
            while True:
                kind, payload = self.messages.get_nowait()
                if kind == "output":
                    output.append(str(payload))
                elif kind == "done":
                    if output:
                        self._append("".join(output))
                        output.clear()
                    code = int(payload)
                    self._append(f"\nЗавершено с кодом {code}.\n")
                    self.process = None
                    self._set_running(False)
                elif kind == "error":
                    self._append(f"\nОшибка запуска: {payload}\n")
                    self.process = None
                    self._set_running(False)
        except queue.Empty:
            pass
        if output:
            self._append("".join(output))
        self.after(70, self._drain_messages)


def main() -> int:
    parser = argparse.ArgumentParser(description="Acoustic File Transfer UI")
    parser.add_argument("--binary", help="путь к acoustic-transfer")
    arguments = parser.parse_args()
    try:
        binary = find_binary(arguments.binary)
    except FileNotFoundError as error:
        root = tk.Tk()
        root.withdraw()
        messagebox.showerror("Acoustic File Transfer", str(error))
        root.destroy()
        return 1
    app = AcousticUi(binary)
    if os.environ.get("ACOUSTIC_UI_SMOKE_TEST") == "1":
        app.withdraw()
        app.update_idletasks()
        print("Tkinter UI smoke: OK")
        app.destroy()
        return 0
    app.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

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


SOURCE_ROOT = Path(__file__).resolve().parents[1]
ROOT = Path(getattr(sys, "_MEIPASS", SOURCE_ROOT))

PROFILE_LABELS = {
    "Турбо": "turbo",
    "Быстрый": "fast",
    "Сбалансированный": "balanced",
    "Устойчивый": "robust",
}

PROFILE_DESCRIPTIONS = {
    "Турбо": "До 5,3× быстрее · короткая дистанция · проверено при SNR 12 dB",
    "Быстрый": "Совместимый скоростной режим · обычные динамики и микрофоны",
    "Сбалансированный": "Рекомендуемый режим · баланс скорости и устойчивости",
    "Устойчивый": "Самый устойчивый режим · шумная комната · работает медленнее",
}

BUTTON_HELP = {
    "Принять файл": (
        "Записывает звук с микрофона, находит передачу и сохраняет проверенный файл. "
        "Эту кнопку нужно нажать первой — до запуска передатчика. Приёмник умеет "
        "пропускать посторонние звуки, автоматически определять профиль передачи "
        "и завершает запись после 5 секунд устойчивой тишины."
    ),
    "Передать файл": (
        "Предлагает выбрать файл, кодирует его в звуковой сигнал и воспроизводит "
        "через динамик. На другом компьютере уже должен работать приём."
    ),
    "Проверить оборудование": (
        "Проверяет, видит ли приложение динамик и микрофон. Это быстрая проверка "
        "устройств, но не проверка качества акустического канала."
    ),
    "Калибровать канал": (
        "Воспроизводит известный тестовый сигнал, записывает его микрофоном и показывает "
        "SNR, синхронизацию, clipping и рекомендацию по профилю."
    ),
    "Оценить передачу": (
        "Ничего не воспроизводит. Заранее показывает длительность, размер WAV, память, "
        "goodput и эффективность выбранного файла."
    ),
    "Тест эффективности": (
        "Запускает локальный цифровой benchmark на 256 байтах и показывает airtime, "
        "goodput, эффективность и скорость декодирования."
    ),
    "Создать WAV": (
        "Кодирует выбранный файл в WAV. Полезно для тестирования, архива или передачи "
        "звука другим способом."
    ),
    "Восстановить из WAV": (
        "Декодирует ранее созданную или записанную WAV-дорожку и сохраняет исходный файл "
        "после проверки CRC32 и SHA-256."
    ),
    "Самопроверка": (
        "Проверяет modem, framing, пакеты и контроль целостности без динамика и микрофона."
    ),
}


class Tooltip:
    """Small delayed tooltip with no third-party UI dependency."""

    def __init__(self, widget: tk.Misc, text: str) -> None:
        self.widget = widget
        self.text = text
        self.window: tk.Toplevel | None = None
        self.pending: str | None = None
        widget.bind("<Enter>", self._schedule, add="+")
        widget.bind("<Leave>", self._hide, add="+")
        widget.bind("<ButtonPress>", self._hide, add="+")

    def _schedule(self, _event: tk.Event[tk.Misc]) -> None:
        self.pending = self.widget.after(450, self._show)

    def _show(self) -> None:
        if self.window is not None:
            return
        x = self.widget.winfo_rootx() + 12
        y = self.widget.winfo_rooty() + self.widget.winfo_height() + 8
        self.window = tk.Toplevel(self.widget)
        self.window.wm_overrideredirect(True)
        self.window.wm_geometry(f"+{x}+{y}")
        tk.Label(
            self.window,
            text=self.text,
            justify="left",
            wraplength=360,
            bg="#eef7ff",
            fg="#132238",
            relief="solid",
            borderwidth=1,
            padx=10,
            pady=8,
            font=("Segoe UI", 9),
        ).pack()

    def _hide(self, _event: tk.Event[tk.Misc] | None = None) -> None:
        if self.pending is not None:
            self.widget.after_cancel(self.pending)
            self.pending = None
        if self.window is not None:
            self.window.destroy()
            self.window = None


def find_binary(explicit: str | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))
    if os.environ.get("ACOUSTIC_TRANSFER_BIN"):
        candidates.append(Path(os.environ["ACOUSTIC_TRANSFER_BIN"]))
    executable = "acoustic-transfer.exe" if os.name == "nt" else "acoustic-transfer"
    candidates.extend(
        [
            ROOT / executable,
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
        self.tooltips: list[Tooltip] = []
        self.title("HEX Acoustic — передача файлов через звук")
        screen_width = self.winfo_screenwidth()
        screen_height = self.winfo_screenheight()
        window_width = min(1080, max(720, screen_width - 48))
        window_height = min(790, max(640, screen_height - 72))
        window_x = max(0, (screen_width - window_width) // 2)
        window_y = max(0, (screen_height - window_height) // 2)
        self.geometry(f"{window_width}x{window_height}+{window_x}+{window_y}")
        self.minsize(min(900, window_width), min(700, window_height))
        self.configure(bg="#0a1018")
        self.option_add("*Font", ("Segoe UI", 10))
        self._configure_style()
        self._build_page()
        self.after(70, self._drain_messages)

    def _configure_style(self) -> None:
        style = ttk.Style(self)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure("Page.TFrame", background="#0a1018")
        style.configure("Panel.TFrame", background="#111b27")
        style.configure("Card.TFrame", background="#152231")
        style.configure("Guide.TFrame", background="#102a35")
        style.configure("Title.TLabel", background="#0a1018", foreground="#f4f8fc", font=("Segoe UI", 23, "bold"))
        style.configure("Eyebrow.TLabel", background="#0a1018", foreground="#52ddb5", font=("Segoe UI", 9, "bold"))
        style.configure("Muted.TLabel", background="#0a1018", foreground="#8fa3b8", font=("Segoe UI", 10))
        style.configure("Panel.TLabel", background="#111b27", foreground="#c3d0dd", font=("Segoe UI", 10))
        style.configure("PanelTitle.TLabel", background="#111b27", foreground="#f0f5fa", font=("Segoe UI", 12, "bold"))
        style.configure("CardTitle.TLabel", background="#152231", foreground="#f4f8fc", font=("Segoe UI", 13, "bold"))
        style.configure("CardBody.TLabel", background="#152231", foreground="#a9bbcb", font=("Segoe UI", 9))
        style.configure("Guide.TLabel", background="#102a35", foreground="#c8f7ea", font=("Segoe UI", 10))
        style.configure("Profile.TLabel", background="#111b27", foreground="#63d8b6", font=("Segoe UI", 9))
        style.configure("Ready.TLabel", background="#0a1018", foreground="#55d6a9", font=("Segoe UI", 10, "bold"))
        style.configure("Busy.TLabel", background="#0a1018", foreground="#ffc857", font=("Segoe UI", 10, "bold"))
        style.configure("Error.TLabel", background="#0a1018", foreground="#ff7a93", font=("Segoe UI", 10, "bold"))
        style.configure("Receive.TButton", background="#2fd3a2", foreground="#06150f", padding=(18, 12), font=("Segoe UI", 11, "bold"))
        style.map("Receive.TButton", background=[("active", "#62e4bd"), ("disabled", "#35594f")])
        style.configure("Send.TButton", background="#4f9cf9", foreground="#071321", padding=(18, 12), font=("Segoe UI", 11, "bold"))
        style.map("Send.TButton", background=[("active", "#78b5ff"), ("disabled", "#334b65")])
        style.configure("Action.TButton", background="#213348", foreground="#e9f1f8", padding=(13, 9), font=("Segoe UI", 9))
        style.map("Action.TButton", background=[("active", "#2d4661"), ("disabled", "#1a2735")])
        style.configure("Link.TButton", background="#0a1018", foreground="#9fc3e8", padding=(10, 7), font=("Segoe UI", 9))
        style.map("Link.TButton", foreground=[("active", "#d1e8ff")])
        style.configure("Danger.TButton", background="#482633", foreground="#ffc0ce", padding=(12, 8), font=("Segoe UI", 9))
        style.map("Danger.TButton", background=[("active", "#633044"), ("disabled", "#2b2028")])
        style.configure("TCombobox", fieldbackground="#1b2b3b", background="#1b2b3b", foreground="#f1f6fb", arrowcolor="#d5e4ef", padding=7)
        style.configure("TEntry", fieldbackground="#1b2b3b", foreground="#f1f6fb", insertcolor="#ffffff", padding=8)

    def _build_page(self) -> None:
        page = ttk.Frame(self, style="Page.TFrame", padding=(24, 20))
        page.pack(fill="both", expand=True)
        page.columnconfigure(0, weight=1)
        page.rowconfigure(5, weight=1)

        heading = ttk.Frame(page, style="Page.TFrame")
        heading.grid(row=0, column=0, sticky="ew")
        heading.columnconfigure(0, weight=1)
        ttk.Label(heading, text="HEX · ACOUSTIC MODEM", style="Eyebrow.TLabel").grid(row=0, column=0, sticky="w")
        ttk.Label(heading, text="Передача файлов через звук", style="Title.TLabel").grid(row=1, column=0, sticky="w", pady=(2, 0))
        ttk.Label(heading, text="Локально · без сети · с проверкой целостности", style="Muted.TLabel").grid(row=2, column=0, sticky="w", pady=(4, 0))
        self.status = ttk.Label(heading, text="● Готово", style="Ready.TLabel")
        self.status.grid(row=0, column=1, sticky="e")
        help_button = ttk.Button(heading, text="Что делает каждая кнопка?", command=self._show_help, style="Link.TButton")
        help_button.grid(row=1, column=1, rowspan=2, sticky="e")

        guide = ttk.Frame(page, style="Guide.TFrame", padding=(16, 11))
        guide.grid(row=1, column=0, sticky="ew", pady=(16, 10))
        ttk.Label(
            guide,
            text="Быстрый старт: на принимающем ПК нажмите «Принять файл», затем на втором ПК — «Передать файл».",
            style="Guide.TLabel",
        ).pack(anchor="w")

        settings = ttk.Frame(page, style="Panel.TFrame", padding=(16, 13))
        settings.grid(row=2, column=0, sticky="ew", pady=(0, 10))
        settings.columnconfigure(2, weight=1)
        ttk.Label(settings, text="Режим передачи", style="Panel.TLabel").grid(row=0, column=0, sticky="w")
        self.profile = tk.StringVar(value="Турбо")
        self.profile_box = ttk.Combobox(
            settings,
            textvariable=self.profile,
            values=tuple(PROFILE_LABELS),
            state="readonly",
            width=18,
        )
        self.profile_box.grid(row=1, column=0, padx=(0, 20), pady=(4, 0), sticky="w")
        self.profile_box.bind("<<ComboboxSelected>>", self._profile_changed)
        ttk.Label(settings, text="Усиление записи", style="Panel.TLabel").grid(row=0, column=1, sticky="w")
        self.gain = tk.StringVar(value="0")
        ttk.Entry(settings, textvariable=self.gain, width=13).grid(row=1, column=1, padx=(0, 22), pady=(4, 0), sticky="w")
        self.profile_description = ttk.Label(
            settings,
            text=PROFILE_DESCRIPTIONS["Турбо"],
            style="Profile.TLabel",
        )
        self.profile_description.grid(row=1, column=2, sticky="w", pady=(4, 0))
        ttk.Label(settings, text="0 = автоматически", style="Panel.TLabel").grid(row=2, column=1, sticky="w", pady=(3, 0))

        self.action_buttons: list[ttk.Button] = []

        workflow = ttk.Frame(page, style="Page.TFrame")
        workflow.grid(row=3, column=0, sticky="ew", pady=(0, 10))
        workflow.columnconfigure(0, weight=1, uniform="workflow")
        workflow.columnconfigure(1, weight=1, uniform="workflow")

        receive_card = ttk.Frame(workflow, style="Card.TFrame", padding=16)
        receive_card.grid(row=0, column=0, sticky="nsew", padx=(0, 5))
        ttk.Label(receive_card, text="1 · Принимающий компьютер", style="CardTitle.TLabel").pack(anchor="w")
        ttk.Label(receive_card, text="Начните запись первой. После передачи она сама остановится через 5 секунд тишины.", style="CardBody.TLabel", wraplength=430).pack(anchor="w", pady=(5, 12))
        self._add_action(receive_card, "Принять файл", self.receive_file, "Receive.TButton").pack(fill="x")

        send_card = ttk.Frame(workflow, style="Card.TFrame", padding=16)
        send_card.grid(row=0, column=1, sticky="nsew", padx=(5, 0))
        ttk.Label(send_card, text="2 · Передающий компьютер", style="CardTitle.TLabel").pack(anchor="w")
        ttk.Label(send_card, text="Выберите файл после того, как на принимающем устройстве уже включена запись.", style="CardBody.TLabel", wraplength=430).pack(anchor="w", pady=(5, 12))
        self._add_action(send_card, "Передать файл", self.send_file, "Send.TButton").pack(fill="x")

        tools = ttk.Frame(page, style="Panel.TFrame", padding=(14, 12))
        tools.grid(row=4, column=0, sticky="ew", pady=(0, 10))
        for column in range(4):
            tools.columnconfigure(column, weight=1, uniform="tools")
        ttk.Label(tools, text="Проверка и дополнительные инструменты", style="PanelTitle.TLabel").grid(row=0, column=0, columnspan=4, sticky="w", pady=(0, 7))
        tool_buttons = [
            ("Проверить оборудование", lambda: self.run_command(["check-audio"])),
            ("Калибровать канал", self.calibrate),
            ("Оценить передачу", self.estimate_file),
            ("Тест эффективности", lambda: self.run_command(["benchmark", "256", *self.profile_args()])),
            ("Создать WAV", self.encode_file),
            ("Восстановить из WAV", self.decode_file),
            ("Самопроверка", lambda: self.run_command(["self-test", *self.profile_args()])),
        ]
        for index, (label, callback) in enumerate(tool_buttons):
            self._add_action(tools, label, callback, "Action.TButton").grid(
                row=1 + index // 4, column=index % 4, sticky="ew", padx=4, pady=4
            )

        console_panel = ttk.Frame(page, style="Panel.TFrame", padding=14)
        console_panel.grid(row=5, column=0, sticky="nsew")
        console_panel.columnconfigure(0, weight=1)
        console_panel.rowconfigure(1, weight=1)
        toolbar = ttk.Frame(console_panel, style="Panel.TFrame")
        toolbar.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        toolbar.columnconfigure(0, weight=1)
        ttk.Label(toolbar, text="Результат и диагностика", style="PanelTitle.TLabel").grid(row=0, column=0, sticky="w")
        self.cancel_button = ttk.Button(toolbar, text="Остановить", command=self.cancel, style="Danger.TButton", state="disabled")
        self.cancel_button.grid(row=0, column=1, padx=(8, 0))
        ttk.Button(toolbar, text="Очистить", command=lambda: self.console.delete("1.0", "end"), style="Action.TButton").grid(row=0, column=2, padx=(8, 0))
        self.console = tk.Text(console_panel, bg="#09131d", fg="#d2dee8", insertbackground="#ffffff", selectbackground="#285a7b", relief="flat", padx=13, pady=11, font=("Cascadia Mono", 9), wrap="word", height=9)
        self.console.grid(row=1, column=0, sticky="nsew")
        scrollbar = ttk.Scrollbar(console_panel, orient="vertical", command=self.console.yview)
        scrollbar.grid(row=1, column=1, sticky="ns")
        self.console.configure(yscrollcommand=scrollbar.set)
        self._append("Готово к работе. Для передачи сначала запустите приём на втором компьютере.\n")

    def _add_action(
        self,
        parent: tk.Misc,
        label: str,
        callback: object,
        style: str,
    ) -> ttk.Button:
        button = ttk.Button(parent, text=label, command=callback, style=style)
        self.action_buttons.append(button)
        if label in BUTTON_HELP:
            self.tooltips.append(Tooltip(button, BUTTON_HELP[label]))
        return button

    def _profile_changed(self, _event: tk.Event[tk.Misc] | None = None) -> None:
        self.profile_description.configure(
            text=PROFILE_DESCRIPTIONS.get(self.profile.get(), "Пользовательский профиль")
        )

    def _show_help(self) -> None:
        window = tk.Toplevel(self)
        window.title("Что делает каждая кнопка")
        window.geometry("720x620")
        window.minsize(560, 460)
        window.configure(bg="#0a1018")
        window.transient(self)

        header = ttk.Frame(window, style="Page.TFrame", padding=(22, 18, 22, 10))
        header.pack(fill="x")
        ttk.Label(header, text="Как пользоваться приложением", style="Title.TLabel").pack(anchor="w")
        ttk.Label(
            header,
            text="Главное правило: сначала включите приём, затем запускайте передачу.",
            style="Muted.TLabel",
        ).pack(anchor="w", pady=(5, 0))

        body = ttk.Frame(window, style="Panel.TFrame", padding=16)
        body.pack(fill="both", expand=True, padx=22, pady=(0, 14))
        body.columnconfigure(0, weight=1)
        body.rowconfigure(0, weight=1)
        text = tk.Text(
            body,
            bg="#111b27",
            fg="#d4e0e9",
            relief="flat",
            wrap="word",
            padx=8,
            pady=6,
            font=("Segoe UI", 10),
        )
        text.grid(row=0, column=0, sticky="nsew")
        scrollbar = ttk.Scrollbar(body, orient="vertical", command=text.yview)
        scrollbar.grid(row=0, column=1, sticky="ns")
        text.configure(yscrollcommand=scrollbar.set)
        text.tag_configure("title", foreground="#63d8b6", font=("Segoe UI", 11, "bold"), spacing1=8, spacing3=3)
        text.tag_configure("body", foreground="#c5d2dd", lmargin2=14, spacing3=7)
        for label, description in BUTTON_HELP.items():
            text.insert("end", label + "\n", "title")
            text.insert("end", description + "\n", "body")
        text.configure(state="disabled")
        ttk.Button(window, text="Понятно", command=window.destroy, style="Receive.TButton").pack(pady=(0, 18))

    def _validate_gain(self) -> bool:
        try:
            gain = float(self.gain.get().replace(",", "."))
            if gain != 0.0 and not 0.1 <= gain <= 50.0:
                raise ValueError
            self.gain.set(f"{gain:g}")
            return True
        except ValueError:
            messagebox.showerror(
                "Проверьте усиление",
                "Введите 0 для автоматического режима или число от 0,1 до 50.",
            )
            return False

    def profile_args(self) -> list[str]:
        return ["--profile", PROFILE_LABELS[self.profile.get()]]

    def _choose_input(self, title: str, filetypes: list[tuple[str, str]] | None = None) -> str:
        return filedialog.askopenfilename(title=title, filetypes=filetypes or [("Все файлы", "*.*")])

    def send_file(self) -> None:
        source = self._choose_input("Выберите файл для передачи")
        if source:
            self.run_command(["send", source, *self.profile_args()])

    def receive_file(self) -> None:
        if not self._validate_gain():
            return
        destination = filedialog.askdirectory(title="Папка для принятых файлов")
        if destination:
            self.run_command(["receive", destination, "--gain", self.gain.get(), *self.profile_args()])

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
        if not self._validate_gain():
            return
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
        self._append("\n▶ Запуск: " + " ".join(f'"{part}"' if " " in part else part for part in arguments) + "\n")
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
        self.status.configure(
            text="● Выполняется" if running else "● Готово",
            style="Busy.TLabel" if running else "Ready.TLabel",
        )
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
                    self._append("\n✓ Операция завершена успешно.\n" if code == 0 else f"\n✕ Операция завершилась с ошибкой (код {code}).\n")
                    self.process = None
                    self._set_running(False)
                    if code != 0:
                        self.status.configure(text="● Нужна проверка", style="Error.TLabel")
                elif kind == "error":
                    self._append(f"\nОшибка запуска: {payload}\n")
                    self.process = None
                    self._set_running(False)
                    self.status.configure(text="● Ошибка запуска", style="Error.TLabel")
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
    smoke_test = os.environ.get("ACOUSTIC_UI_SMOKE_TEST")
    if smoke_test:
        app.withdraw()
        app.update_idletasks()
        if smoke_test == "backend":
            startup = None
            creationflags = 0
            if os.name == "nt":
                startup = subprocess.STARTUPINFO()
                startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                creationflags = subprocess.CREATE_NO_WINDOW
            result = subprocess.run(
                [str(binary), "self-test", *app.profile_args()],
                cwd=ROOT,
                startupinfo=startup,
                creationflags=creationflags,
                check=False,
            )
            app.destroy()
            return result.returncode
        print("Tkinter UI smoke: OK")
        app.destroy()
        return 0
    app.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

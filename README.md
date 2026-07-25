# Acoustic File Transfer

Консольное приложение для передачи произвольных файлов через динамик и
микрофон. Файл преобразуется в синхронизированный FSK-сигнал, а после приёма
проверяется по CRC32 каждого блока и SHA-256 всего содержимого.

В коде есть backend для Linux, macOS и Windows, три профиля канала,
диагностическая калибровка, предварительная оценка передачи и лёгкий
одностраничный UI на Tkinter. Windows-сборка и цифровой WAV-cycle проверены
локально; Linux/macOS и реальный акустический тракт требуют результатов CI и
полевой device matrix.

## Статус проекта

Текущая версия 0.5.0 — **лабораторный acoustic file transfer prototype**. Она умеет
передать и проверить файл, но пока не является production-ready модемом:

- вся попытка передаётся одним большим аудиокадром;
- профиль выбирается вручную на обоих устройствах;
- нет FEC, ACK/NACK, selective repeat и resume;
- live path не потоковый: сначала создаётся или записывается полный WAV;
- SHA-256 подтверждает целостность, но не отправителя;
- калибровочный SNR является ориентировочной диагностикой.

Подробный фактический разбор: [инженерный аудит](docs/AUDIT.md). Порядок
превращения прототипа в современный модем: [roadmap](docs/ROADMAP.md).

## Возможности

- живая передача через динамик и микрофон;
- детерминированный режим `файл → WAV → файл`;
- автоматический поиск chirp-преамбулы;
- поиск рассогласования аудиочасов в диапазоне ±2% (автотест покрывает 0,8%);
- частотно-толерантное FSK-распознавание;
- автоматическое или ручное усиление записи;
- профили `fast`, `balanced` и `robust`;
- команды `calibrate` и `estimate`;
- измерение goodput/airtime через `benchmark` и воспроизводимый `channel-test`;
- машинно-читаемый JSON для оценки и тестов эффективности;
- receiver metrics: chirp correlation, clock error, confidence, frequency offset
  и clipping;
- безопасные уникальные временные файлы и атомарное сохранение результата;
- защита от случайной перезаписи;
- графическая панель без Electron, браузера и web-сервера.

## Сборка

Требуются CMake 3.16+ и компилятор с поддержкой C++20.

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

На Linux также доступен `make && make test`. Для live audio нужен пакет
`alsa-utils`. Windows использует Windows Multimedia API, macOS — CoreAudio и
системный `afplay`. Наличие backend-кода не заменяет проверку на реальном
оборудовании; результаты совместимости должны фиксироваться в device matrix.

После сборки выполните:

```bash
./build/acoustic-transfer --help
./build/acoustic-transfer self-test
```

При генераторе Visual Studio бинарный файл обычно находится в
`build/Release/acoustic-transfer.exe`.

## UI

UI использует стандартный Python 3.10+ с Tkinter и запускает тот же C++ CLI:

```bash
./build/acoustic-transfer ui
```

Также можно запустить `python3 ui/acoustic_ui.py`. На Linux Tkinter иногда нужно
установить отдельным пакетом `python3-tk`. UI остаётся одним окном и не держит в
памяти Chromium или локальный сервер. Причины выбора и production-компромиссы
описаны в [документе о UI](docs/UI.md).

## Основные команды

```bash
acoustic-transfer profiles
acoustic-transfer estimate input.png --profile balanced
acoustic-transfer benchmark 256 --profile balanced
acoustic-transfer channel-test 128 --profile balanced
acoustic-transfer calibrate --profile balanced

acoustic-transfer encode input.png transmission.wav --profile balanced
acoustic-transfer decode transmission.wav restored.png --profile balanced

acoustic-transfer receive artifacts/received --seconds 30 --profile balanced
acoustic-transfer send input.png --profile balanced
```

Приёмник запускается первым. На обоих устройствах должен быть выбран один
профиль. `receive` принимает `--gain 0` для автоматического усиления или число
от `0.1` до `50`.

Существующий файл не перезаписывается без `--force`. При приёме в папку
совпадающему имени автоматически добавляется номер копии.

## Профили

| Профиль | Модуляция | Канальный bitrate | Блок | Назначение |
|---|---:|---:|---:|---|
| `fast` | 4-FSK | 600 бит/с | 1024 B | тихая комната, короткая дистанция |
| `balanced` | 4-FSK | 400 бит/с | 512 B | режим по умолчанию |
| `robust` | 2-FSK × 3 | 66 бит/с | 256 B | шум и реверберация |

Параметры находятся в `config/*.conf` и действительно загружаются приложением.

Подробности: [запуск](docs/RUN.md), [архитектура](docs/ARCHITECTURE.md),
[демонстрация](docs/DEMO.md), [ограничения](docs/LIMITATIONS.md),
[эффективность](docs/PERFORMANCE.md), [выбор UI](docs/UI.md),
[аудит](docs/AUDIT.md), [roadmap](docs/ROADMAP.md).

## Структура

```text
config/             профили модема и кадра
docs/               инструкции и техническое описание
include/acoustic/   публичные C++-интерфейсы
src/                CLI, протокол, модем и аудиобэкенды
tests/              автоматические тесты
ui/                 одностраничная Tkinter-панель
artifacts/           демонстрационные данные и результаты
```

## Лицензия

Учебный прототип для хакатона.

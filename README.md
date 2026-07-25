# Acoustic File Transfer

Консольная MVP-система передачи произвольных файлов через динамик и микрофон. Проект
кодирует файл в пакетный звуковой сигнал, принимает его на другом устройстве,
восстанавливает исходные байты и проверяет целостность.

> Работает полный цикл `файл → FSK/WAV → файл`, живые динамик и микрофон,
> автоматический поиск chirp-преамбулы, разбиение на блоки и проверка CRC32 и
> SHA-256.

## Быстрый запуск

Требования: Linux/macOS, компилятор с поддержкой C++20 и `make`.

```bash
make
./build/acoustic-transfer --help
./build/acoustic-transfer self-test
```

Для обычного запуска без параметров открывается интерактивное меню:

```bash
./build/acoustic-transfer
```

Команды выбираются цифрами `1`–`4`; `0` завершает программу. Во время записи и
воспроизведения отображается полоса прогресса.

Альтернативная сборка через CMake 3.16+:

```bash
cmake -S . -B build
cmake --build build
./build/acoustic-transfer self-test
```

Подробные инструкции находятся в [docs/RUN.md](docs/RUN.md), сценарий показа
экспертам — в [docs/DEMO.md](docs/DEMO.md).

## Передача через WAV

```bash
./build/acoustic-transfer encode input.png transmission.wav

./build/acoustic-transfer decode transmission.wav restored.png
cmp input.png restored.png
```

## Живая передача через динамик и микрофон

```bash
./build/acoustic-transfer send input.png
./build/acoustic-transfer receive received/ 30
```

На Linux команды используют `aplay` и `arecord` из пакета `alsa-utils`.
Получатель запускается первым; длительность записи можно указать последним
аргументом. Усиление микрофонной записи работает автоматически. При
необходимости укажите ручной коэффициент последним аргументом:

```bash
./build/acoustic-transfer receive artifacts/received 30 4
```

Здесь `30` — длительность записи в секундах, `4` — усиление в четыре раза.

Архитектура и формат пакетов описаны в [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md),
известные ограничения — в [docs/LIMITATIONS.md](docs/LIMITATIONS.md).

## Структура репозитория

```text
artifacts/          тестовые файлы, WAV-записи и результаты
config/             профили скорости и надёжности
docs/               запуск, демонстрация и техническое описание
include/acoustic/   публичные C++-заголовки
src/                реализация CLI, протокола, модема и аудио
tests/              автоматические тесты
```

## Лицензия

Учебный прототип для хакатона.

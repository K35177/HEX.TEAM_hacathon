# Acoustic File Transfer

MVP-система передачи произвольных файлов через динамик и микрофон. Проект
кодирует файл в пакетный звуковой сигнал, принимает его на другом устройстве,
восстанавливает исходные байты и проверяет целостность.

> Текущий этап: работает полный цикл `файл → FSK/WAV → файл`, разбиение на
> блоки и проверка CRC32. Живые динамик и микрофон будут добавлены следующим
> этапом.

## Быстрый запуск

Требования: Linux/macOS, компилятор с поддержкой C++20 и `make`.

```bash
make
./build/acoustic-transfer --help
./build/acoustic-transfer self-test
```

Альтернативная сборка через CMake 3.16+:

```bash
cmake -S . -B build
cmake --build build
./build/acoustic-transfer self-test
```

Подробные инструкции находятся в [docs/RUN.md](docs/RUN.md), сценарий показа
экспертам — в [docs/DEMO.md](docs/DEMO.md).

## Работающие команды

```bash
./build/acoustic-transfer encode input.png transmission.wav

./build/acoustic-transfer decode transmission.wav restored.png
cmp input.png restored.png
```

## Планируемые команды живого аудио

```bash
./build/acoustic-transfer send input.png
./build/acoustic-transfer receive received/
```

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

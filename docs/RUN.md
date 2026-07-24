# Инструкция по запуску

## Системные требования

- Linux или macOS;
- C++20-компилятор (`g++` 10+ или `clang++` 12+);
- GNU Make либо CMake 3.16+;
- на этапе живого аудио понадобятся динамик и микрофон.

## Сборка

Из корня репозитория:

```bash
make
./build/acoustic-transfer --help
make test
```

Через CMake:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Проверка

```bash
./build/acoustic-transfer self-test
```

Команда проверяет полный цикл в памяти: файл, пакеты, FSK-модуляцию,
демодуляцию, сборку файла и CRC32.

## Передача через WAV

```bash
./build/acoustic-transfer encode artifacts/samples/demo.txt artifacts/recordings/demo.wav
./build/acoustic-transfer decode artifacts/recordings/demo.wav artifacts/received/demo.txt
cmp artifacts/samples/demo.txt artifacts/received/demo.txt
```

`encode` выводит размер и CRC32 исходного файла. `decode` проверяет CRC каждого
пакета и всего восстановленного файла, а затем выводит `integrity OK`.

## Целевой запуск через динамик и микрофон

Передатчик:

```bash
./build/acoustic-transfer send artifacts/samples/demo.txt
```

Приёмник (запускается первым):

```bash
./build/acoustic-transfer receive artifacts/received
```

Устройства следует поставить на расстоянии 0,5–1 м, отключить обработку
микрофона и начать с громкости динамика около 60–70%.

Команды `send` и `receive` пока зарезервированы и сообщают, что live audio
будет реализовано на следующем этапе.

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

## Текущий запуск

```bash
./build/acoustic-transfer self-test
```

Команда проверяет сериализацию пакета, CRC32 и генерацию FSK-сэмплов. На
текущем этапе `encode`, `decode`, `send` и `receive` уже зарезервированы в CLI,
но будут подключаться последовательно в следующих версиях.

## Целевой запуск MVP

Передатчик:

```bash
./build/acoustic-transfer send artifacts/samples/demo.txt
```

Приёмник (запускается первым):

```bash
./build/acoustic-transfer receive artifacts/received
```

Для воспроизводимой проверки без влияния помещения:

```bash
./build/acoustic-transfer encode artifacts/samples/demo.txt artifacts/recordings/demo.wav
./build/acoustic-transfer decode artifacts/recordings/demo.wav artifacts/received
```

Устройства следует поставить на расстоянии 0,5–1 м, отключить обработку
микрофона и начать с громкости динамика около 60–70%.

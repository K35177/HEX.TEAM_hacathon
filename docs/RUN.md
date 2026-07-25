# Инструкция по запуску

## Требования

- Linux, macOS или Windows;
- C++20-компилятор;
- CMake 3.16+;
- для UI — Python 3.10+ с Tkinter;
- только для Linux live audio — `alsa-utils` (`aplay`, `arecord`).

## Сборка и тесты

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Linux/macOS с одноконфигурационным генератором обычно создают
`build/acoustic-transfer`. Visual Studio создаёт
`build/Release/acoustic-transfer.exe`.

На Linux можно использовать GNU Make:

```bash
make
make test
```

## Первичная проверка

```bash
acoustic-transfer check-audio
acoustic-transfer self-test --profile balanced
acoustic-transfer profiles
```

`check-audio` показывает backend и базовое наличие устройств, но не доказывает
работоспособность акустического тракта. `self-test` проверяет кадрирование,
chirp, FSK, пакеты, CRC32 и SHA-256 без реального звука.

## Оценка до передачи

```bash
acoustic-transfer estimate artifacts/samples/demo.txt --profile balanced
```

Команда показывает частоты, число блоков, длительность сигнала, рекомендуемое
время записи, размер WAV, пиковую память, goodput и долю полезных бит в airtime.

## Измерение эффективности

```bash
acoustic-transfer benchmark 256 --profile balanced
acoustic-transfer channel-test 128 --profile balanced
acoustic-transfer estimate input.png --profile balanced --json
```

`benchmark` измеряет чистый цифровой round-trip и CPU/realtime factor.
`channel-test` прогоняет детерминированные AWGN, clock drift и clipping scenarios
и считает completion/delivery efficiency. Это симулятор, а не замена реальному
акустическому тесту. Все три команды поддерживают `--json`.

## Калибровка

```bash
acoustic-transfer calibrate --profile balanced
```

Программа начинает запись, воспроизводит известный проверочный кадр, оценивает
noise/signal windows и пытается полностью восстановить данные. Выводятся SNR
estimate, chirp correlation, confidence, clock/frequency offset и clipping. Это
ещё не лабораторное измерение: текущая версия не строит noise PSD и не измеряет
response по каждой частоте. Если канал слабый, будет рекомендован `robust`.
Устройства должны находиться в рабочем положении.

## Передача через WAV

```bash
acoustic-transfer encode input.png transmission.wav --profile balanced
acoustic-transfer decode transmission.wav restored.png --profile balanced
```

Для разрешённой перезаписи добавьте `--force`. Для декодирования записи с тихим
сигналом используйте `--gain 0` (auto) или, например, `--gain 4`.

## Живая передача

Сначала на принимающем устройстве:

```bash
acoustic-transfer receive artifacts/received --seconds 30 --profile balanced
```

Затем на передающем:

```bash
acoustic-transfer send input.png --profile balanced
```

Точное рекомендуемое значение `--seconds` предварительно показывает `estimate`.
Профиль на устройствах должен совпадать.

Старый позиционный формат также поддерживается:

```text
receive <папка> [секунды] [усиление]
```

## Графическая панель

```bash
acoustic-transfer ui
```

В одном окне доступны отправка, приём, WAV-кодирование, декодирование, estimate,
benchmark, калибровка, проверка аудио и self-test. Долгая операция выполняется в
отдельном процессе; её можно остановить кнопкой. Обоснование Tkinter и варианты
production-упаковки: [UI.md](UI.md).

## Практические рекомендации

- расстояние между устройствами: 0,5–1 м;
- начальная громкость динамика: 60–70%;
- отключите подавление шума и автоматические «улучшения» микрофона;
- сначала выполните `calibrate`;
- для шумного помещения выберите `robust`, учитывая его низкую скорость.

## Интерпретация результата

`integrity OK` означает, что принятые байты совпали с заявленным SHA-256. Это не
означает аутентификацию отправителя и не защищает от повторного проигрывания
старой записи. Ограничения и актуальная зрелость описаны в
[AUDIT.md](AUDIT.md).

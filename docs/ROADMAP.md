# Roadmap: современный аудиомодем без смены архитектуры

## Принцип развития

Текущие слои сохраняются:

```text
UI/CLI → transfer → packet → framing → modem → audio
```

Изменяется глубина реализации внутри слоёв и вводится protocol version 2. Новая
архитектура приложения не требуется.

## Definition of Done для «настоящего модема»

Система считается современным аудиомодемом, когда она:

1. начинает принимать и декодировать поток до завершения записи;
2. сама обнаруживает peer и согласует совместимый режим;
3. исправляет типовые ошибки FEC и повторяет только потерянные кадры;
4. автоматически выбирает скорость по измеренному каналу;
5. показывает SNR, BER/PER, CFO, SRO, retries и effective throughput;
6. поддерживает resume после прерывания;
7. аутентифицирует control/data и при необходимости шифрует файл;
8. ограничивает память независимо от размера transfer;
9. проходит симулятор канала и реальную device matrix;
10. имеет воспроизводимую сборку, installer и подписанные релизы.

## Этап 0. Честная измеримость

Цель: перестать оценивать качество только по `decoded/not decoded`.

Статус 0.5.0: этап выполняется. Уже есть `ReceiverMetrics`, JSON для
`estimate`/`benchmark`/`channel-test`, goodput/airtime/delivery efficiency,
исправленные noise/signal windows и начальный channel simulator. Остались
per-tone response, единый JSON для live/decode, BER/PER и явное управление
diagnostic recordings.

### Работы

- добавить структуру `ReceiverMetrics`;
- вывести normalized chirp score, estimated gain, clipping ratio;
- считать per-symbol confidence, frequency offset и clock scale;
- считать corrected/uncorrectable frames, retries, goodput;
- добавить `--json` для CLI/UI и diagnostic report;
- исправить calibration: отдельно noise window, signal window, per-tone response;
- сохранять diagnostic WAV только по явному `--keep-recording`.

### Критерии готовности

- один и тот же WAV даёт детерминированный JSON report;
- метрики имеют единицы измерения и documented interpretation;
- ни одна error message не скрывает frame/session context.

## Этап 1. Streaming PHY и protocol v2

Цель: принимать поток онлайн и восстанавливаться на границе каждого кадра.

### Работы

- ввести `AudioStream` с bounded ring buffers и `stop()`;
- унифицировать device enumeration, sample rate, latency и permissions;
- заменить полный WAV в live path на callbacks/read-write stream;
- оставить WAV как test/debug backend;
- разбить transfer на короткие independently decodable frames;
- добавить robust control header:

```text
magic | version | session_id | frame_type | profile_id | coding_id
frame_index | ack_window | payload_length | header_crc/fec
```

- добавить periodic pilots и end-of-frame marker;
- реализовать автоматическую остановку после последнего кадра.

### Критерии готовности

- память live path не растёт с размером файла;
- первый frame декодируется до окончания передачи;
- после испорченного frame следующий снова синхронизируется;
- cancel останавливает capture/playback не более чем за 300 мс;
- 30-минутный soak test не теряет buffers и handles.

## Этап 2. FEC и надёжный half-duplex transport

Цель: повреждение части сигнала не требует повторять весь файл.

### Работы

- soft-decision output демодулятора;
- сильная защита control header;
- первый data coding: convolutional code + soft Viterbi;
- block interleaver против импульсных ошибок;
- внешний Reed–Solomon для burst/erasure recovery;
- ACK/NACK bitmap после окна кадров;
- selective repeat, adaptive timeout и retry budget;
- session manifest и resume bitmap;
- orderly close с подтверждением итогового digest.

### Критерии готовности

- одиночный повреждённый frame повторяется без рестарта transfer;
- burst corruption не длиннее interleaver depth исправляется или локализуется;
- после process restart transfer продолжается с подтверждённого окна;
- duplicate/reordered ACK не повреждает session state.

## Этап 3. Negotiation и адаптивный PHY

Цель: пользователь не выбирает профиль вручную.

### Работы

- фиксированный low-rate control channel;
- discovery и capability exchange;
- channel sounding по нескольким частотам;
- automatic profile/rate selection;
- continuous CFO/SRO tracking по pilots;
- streaming band-pass filter и AGC без hard clipping;
- оценка impulse response и простой adaptive equalizer;
- fast profile v2: рассмотреть MFSK/OFDM/CSS только как новый modem plugin;
- fallback к robust control mode при росте PER.

### Критерии готовности

- peers с разным набором profiles согласуют общий режим;
- profile downgrade/upgrade происходит без потери session;
- неизвестная protocol version завершается понятной ошибкой;
- rate adaptation повышает goodput и не ухудшает completion rate.

Классические training/equalization подходы можно сверять с
[ITU-T V.34](https://www.itu.int/rec/T-REC-V.34), session negotiation — с
[ITU-T V.8](https://www.itu.int/rec/T-REC-V.8/en).

## Этап 4. Аутентификация и приватность

Цель: отличать случайную ошибку от подмены и безопасно передавать приватные
данные.

### Работы

- pairing с коротким кодом/SAS или QR;
- session nonce и monotonic packet numbers;
- key agreement через проверенную библиотеку;
- AEAD для metadata и payload;
- replay protection;
- режим «аутентификация без шифрования» для публичных demo;
- безопасное удаление session keys.

### Критерии готовности

- изменение filename, header или ciphertext всегда обнаруживается;
- повтор старой записи не принимается как новая session;
- nonce reuse исключён тестами и форматом;
- собственная реализация криптопримитивов отсутствует.

Ориентиры: [RFC 8439](https://www.rfc-editor.org/info/rfc8439/) и
[NIST SP 800-38D](https://csrc.nist.gov/pubs/sp/800/38/d/final).

## Этап 5. Productization

### Работы

- выбор input/output devices и проверка permissions в UI;
- installer/package для Windows, macOS и Linux;
- bundled UI runtime или документированная dependency policy;
- code signing и checksums релизов;
- crash-safe resume state;
- localization и accessibility;
- предупреждение о слышимом сигнале и калибровка безопасной громкости;
- privacy policy для microphone recordings;
- compatibility matrix и release notes.

### Критерии готовности

- чистая машина устанавливает и удаляет приложение без ручной настройки PATH;
- все три ОС проходят smoke test на реальных устройствах;
- UI не требует терминала;
- пользователь может выбрать устройства и увидеть реальную latency/sample rate.

## Channel simulator и тестовая матрица

До оптимизации PHY нужен воспроизводимый simulator со следующими impairments:

- AWGN с заданным SNR;
- amplitude clipping и nonlinear distortion;
- carrier/frequency offset;
- sample-rate offset и time-varying drift;
- impulse noise и burst erasures;
- multipath/reverberation с измеренными room impulse responses;
- leading/trailing silence;
- dropped/duplicated sample buffers;
- background speech/music.

### Минимальная автоматическая матрица

| Тест | Диапазон |
|---|---|
| SNR sweep | от -5 до 30 dB |
| Sample-rate offset | ±50, ±200, ±1000 ppm |
| Frequency offset | ±5, ±20, ±50 Hz |
| Clipping | 0%, 0,1%, 1%, 5% samples |
| Burst loss | 1–100 ms |
| Reverberation | несколько реальных room impulse responses |
| Payload | 0 B, 1 B, 1 KiB, 100 KiB, streaming large file |

Результат каждого теста: acquisition probability, BER до FEC, FER после FEC,
PER после CRC, retries, goodput, latency и peak memory.

## Целевые продуктовые показатели

Это цели, а не текущие характеристики.

| Показатель | Цель первой production beta |
|---|---:|
| Discovery + handshake | ≤ 3 с |
| Completion rate, quiet room, 1 м | ≥ 99% для 10 KiB |
| Completion rate, moderate noise | ≥ 95% с retries |
| Unrecoverable transfer | не повреждает уже подтверждённые данные |
| Peak memory live path | ≤ 100 MiB и не зависит от размера файла |
| Cancel latency | ≤ 300 мс |
| Control channel | работает при более низком SNR, чем data profiles |
| Security | authenticated metadata + payload, replay protection |

Скоростные цели должны устанавливаться только после channel simulator и device
matrix. Нельзя объявлять `fast` по raw bitrate без измеренного goodput и
completion rate.

## Первые десять задач в правильном порядке

1. `ReceiverMetrics` и JSON report.
2. Исправленная calibration с per-tone channel response.
3. Channel simulator + golden recordings.
4. Independently framed protocol v2 packets.
5. Streaming audio API и bounded ring buffers.
6. Robust control header и profile negotiation.
7. Soft FSK decisions + convolutional FEC.
8. Interleaving + selective-repeat ACK bitmap.
9. Session resume и orderly close.
10. AEAD/pairing через проверенную crypto library.

Этот порядок даёт измеримый прирост надёжности и сохраняет текущую архитектуру.

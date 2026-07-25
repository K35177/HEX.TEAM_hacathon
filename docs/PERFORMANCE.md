# Эффективность и производительность

Дата измерений: 25 июля 2026 года. Версия: 0.5.0.

## Главная метрика

Основная продуктовая метрика проекта — **delivery efficiency**: сколько полезных
бит файла успешно доставлено за суммарное эфирное время относительно канального
bitrate.

Для одного успешного цифрового цикла также выводятся:

- `channel_bps` — теоретический bitrate выбранного PHY;
- `goodput_bps = payload_bits / airtime`;
- `efficiency_percent = goodput_bps / channel_bps × 100%`;
- `completion_rate` — доля полностью восстановленных передач;
- `decode_realtime_factor = airtime / decode_cpu_time`.

Raw bitrate без completion rate не считается доказательством эффективности:
неуспешная быстрая попытка доставляет 0 полезных бит.

## Команды

```bash
acoustic-transfer estimate <file> --profile balanced
acoustic-transfer benchmark 256 --profile balanced
acoustic-transfer channel-test 128 --profile balanced
```

Для автоматической обработки `estimate`, `benchmark` и `channel-test`
поддерживают `--json`.

## Результаты CPU-оптимизации

Один прогон на текущей Windows-машине, MinGW `-O2`, входной файл 376 B. Это
сравнение до/после на одном оборудовании, а не универсальный performance claim.

| Профиль | Decode до | Decode 0.5.0 | Ускорение |
|---|---:|---:|---:|
| `fast` | 153,4 ms | 44,1 ms | 3,5× |
| `balanced` | 229,0 ms | 61,6 ms | 3,7× |
| `robust` | 625,1 ms | 95,9 ms | 6,5× |

Причины ускорения:

- Goertzel вместо `sin/cos` для каждого tone/sample;
- рекуррентный oscillator в модуляторе;
- блочное чтение и запись PCM WAV;
- чтение stdout UI блоками вместо одного символа;
- совместный поиск chirp start/clock scale без потери измеряемого drift.

## Текущая эффективность чистого цифрового цикла

Payload 256 B, один воспроизводимый прогон `benchmark`:

| Профиль | Airtime | Goodput | Эффективность | Decode CPU |
|---|---:|---:|---:|---:|
| `fast` | 4,78 s | 428,5 bit/s | 71,4% | 29,6 ms |
| `balanced` | 7,08 s | 289,3 bit/s | 72,3% | 34,9 ms |
| `robust` | 41,11 s | 49,8 bit/s | 75,5% | 59,6 ms |

На файле README размером 6707 B эффективность выше из-за амортизации metadata
и преамбулы: 96,9% (`fast`), 95,2% (`balanced`) и 93,0% (`robust`). Увеличенные
размеры packet payload дали заметный выигрыш, потому что protocol v1 пока не умеет
повторять отдельные пакеты и маленькие блоки не давали преимуществ восстановления.

## Симулятор канала

`channel-test 128` проверяет шесть детерминированных сценариев: clean, SNR 24 dB,
drift 4000 ppm, SNR 18 dB + drift 2000 ppm, SNR 12 dB + drift 2000 ppm и сильный
clipping. На текущей версии все три профиля восстановили 6/6 payload.

Это не заменяет room impulse responses, burst loss, фоновые речь/музыку и
hardware matrix. Результат доказывает воспроизводимость алгоритмического теста,
но не дальность или completion rate в реальной комнате.

## Оставшийся главный резерв

CPU уже декодирует в сотни раз быстрее эфирного времени. Поэтому следующий
качественный прирост даст не переписывание UI, а:

1. independently framed protocol v2;
2. soft-decision FEC и interleaving;
3. selective repeat вместо повтора всего файла;
4. автоматический выбор профиля по измеренному каналу;
5. streaming audio без полного WAV в памяти.

Именно эти механизмы должны повышать delivery efficiency в реальном шумном
канале, а не только raw bitrate в лабораторном WAV.

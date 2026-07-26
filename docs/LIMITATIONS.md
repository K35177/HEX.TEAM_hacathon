# Ограничения решения

Этот файл описывает ограничения текущей версии 0.7.2. Подробные причины,
приоритеты и критерии исправления находятся в [AUDIT.md](AUDIT.md) и
[ROADMAP.md](ROADMAP.md).

## Надёжность протокола

- Вся передача находится в одном аудиокадре с одной chirp-преамбулой.
- Внутренний packet sync word не используется для повторного поиска после
  потери байта или символа.
- Frame v2 использует RS(255,191) и межблочный interleaving; код исправляет до
  32 ошибочных байтов в каждом codeword. ACK/NACK, selective repeat и resume
  пока отсутствуют.
- CRC32 и SHA-256 остаются жёсткими воротами целостности. Если число ошибок
  превышает ёмкость FEC, попытку приходится повторять целиком.
- `symbol_repetitions` увеличивает длительность тона, но не является полноценной
  коррекцией ошибок.
- Приёмник автоматически пробует встроенные профили; передатчик всё ещё не
  передаёт capability negotiation и не выполняет automatic rate adaptation.
- `turbo-v1` читает неповреждённые кадры 0.6.1, но старый формат не имел FEC.
- `turbo-1600` оставлен для автоматического чтения WAV 0.7.0; новые передачи его не используют.

## DSP и акустический канал

- Clock scale оценивается по chirp и, только при достаточном SNR, уточняется по
  концу кадра. Диапазон поиска ±2% не является гарантией успешной коррекции
  ±2%; автотесты покрывают 0,8%, отдельный low-SNR тест — 0,2%.
- Нет continuous timing recovery, pilots, adaptive equalizer и оценки multipath.
- Нет отдельного band-pass filter, noise PSD, per-tone response и soft decisions.
- Auto gain не восстанавливает clipping и может усиливать фоновые помехи.
- `calibrate` разделяет noise/signal windows и выводит receiver metrics, но без
  noise PSD и per-tone response это не поверенное измерение SNR.
- Практический результат зависит от реверберации, расстояния, динамиков,
  микрофона и OS audio processing.
- Сигнал слышим; режим безопасной громкости пока не формализован.

## Real-time и производительность

- Live path не end-to-end streaming: `send` сначала создаёт полный WAV, а
  `receive` декодирует только после завершения накопленной записи.
- Capture останавливается после пяти секунд тишины, оцениваемой относительно уровня передачи.
  Защищённого end-of-frame пока нет; постоянный громкий шум всё ещё может отложить остановку.
- Файл, transfer stream и аудиокадр обрабатываются целиком в памяти.
- CPU-path ускорен Goertzel и блочным WAV I/O, но это не устраняет линейный рост
  памяти до появления streaming buffers.
- Скорость PHY до FEC/заголовков: 3200 бит/с (`wideband`), 1200 бит/с (`turbo`), около
  600 бит/с (`fast`), 400 бит/с (`balanced`) и 66 бит/с (`robust`). `turbo`
  занимает 1,2–4,8 кГц. `wideband` доходит до 15 кГц и на микрофонах с
  voice-processing/low-pass может иметь нулевой delivered goodput. Effective
  goodput зависит от размера файла и completion rate.
- Нет формального CPU budget и streaming soak tests.

## Платформы

- Windows backend собран и device discovery проверен локально.
- Linux требует внешние `aplay` и `arecord` из `alsa-utils`.
- macOS playback использует `afplay`, запись — CoreAudio.
- Наличие CI workflow и backend-кода не является доказательством end-to-end
  работы до появления успешных CI и hardware results.
- Нет выбора конкретного input/output device, настройки latency и проверки
  microphone permissions в UI.
- Исходный UI требует Python 3.10+ с Tkinter; Windows one-file EXE уже включает
  runtime, но пока не подписан и не оформлен как installer.

## Безопасность

- SHA-256 обеспечивает целостность содержимого, но не аутентификацию отправителя.
- Filename и control metadata не защищены криптографическим MAC/AEAD.
- Нет шифрования, pairing, session nonce и replay protection.
- Diagnostic microphone recordings не имеют отдельной privacy policy.

## Эксплуатация

- Отмена UI-процесса на Linux/macOS может не гарантировать завершение внешнего
  `aplay`, `arecord` или `afplay`.
- Нет installers, code signing, crash-safe session state и compatibility matrix.
- Физическую дальность и completion rate необходимо измерять на реальных
  комбинациях устройств.

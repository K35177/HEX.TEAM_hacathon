# Ограничения решения

Этот файл описывает ограничения текущей версии 0.6.1. Подробные причины,
приоритеты и критерии исправления находятся в [AUDIT.md](AUDIT.md) и
[ROADMAP.md](ROADMAP.md).

## Надёжность протокола

- Вся передача находится в одном аудиокадре с одной chirp-преамбулой.
- Внутренний packet sync word не используется для повторного поиска после
  потери байта или символа.
- Нет FEC, interleaving, ACK/NACK, selective repeat и resume.
- CRC32 и SHA-256 только обнаруживают ошибку; повреждённую попытку приходится
  повторять целиком.
- `symbol_repetitions` увеличивает длительность тона, но не является полноценной
  коррекцией ошибок.
- Передатчик и приёмник должны заранее выбрать одинаковый профиль.
- Нет handshake, capability negotiation и automatic rate adaptation.

## DSP и акустический канал

- Clock scale оценивается один раз по короткому chirp. Диапазон поиска ±2% не
  является гарантией успешной коррекции ±2%; автотест покрывает 0,8%.
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
- Capture останавливается после пяти секунд тишины, но защищённого маркера конца
  кадра пока нет; постоянный шум может отложить остановку до защитного лимита.
- Файл, transfer stream и аудиокадр обрабатываются целиком в памяти.
- CPU-path ускорен Goertzel и блочным WAV I/O, но это не устраняет линейный рост
  памяти до появления streaming buffers.
- Скорость до заголовков: 3200 бит/с (`wideband`), 1920 бит/с (`turbo`), около
  600 бит/с (`fast`), 400 бит/с (`balanced`) и 66 бит/с (`robust`). `turbo`
  занимает 0,96–8,16 кГц. `wideband` доходит до 15 кГц и на микрофонах с
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

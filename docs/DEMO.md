# Сценарий демонстрации

## Подготовка

1. Собрать приложение на двух устройствах.
2. Выполнить тесты и `self-test --profile balanced`.
3. Проверить `check-audio`.
4. Положить небольшой файл в `artifacts/samples/`.
5. Поставить устройства на расстоянии 0,5–1 м.

## Основной сценарий

1. Открыть `acoustic-transfer ui`.
2. Выбрать `balanced` и нажать «Оценить» — показать длительность и размер WAV.
3. Нажать «Эффективность» — показать goodput, airtime efficiency и CPU factor.
4. Нажать «Калибровать» и показать диагностическую оценку SNR и рекомендацию, отдельно
   пояснив, что это не лабораторное измерение канала.
5. На принимающем устройстве нажать «Принять файл».
6. На передающем нажать «Передать файл».
7. После приёма показать `integrity OK`, SHA-256 и восстановленный файл.

Для шумного помещения заранее выбрать `robust`; длительность обязательно взять
из `estimate`.

## Резервный сценарий

```bash
acoustic-transfer encode artifacts/samples/demo.txt artifacts/recordings/demo.wav --profile balanced --force
acoustic-transfer decode artifacts/recordings/demo.wav artifacts/received/demo.txt --profile balanced --force
```

Этот цикл проверяет тот же transfer protocol, chirp, FSK, CRC32 и SHA-256, но не
доказывает устойчивость физического акустического канала.

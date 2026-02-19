# ESP32-S3 standaard ESP-IDF project (CLI, Linux)

Dit is een **minimaal ESP-IDF** project (zonder PlatformIO) voor:

- ESP32-S3
- 16MB flash
- 8MB PSRAM
- W5500 (SPI ethernet)
- LED blink via `esp_timer`

## Projectstructuur

```text
examples/esp32s3_idf_minimal/
├── CMakeLists.txt
├── sdkconfig.defaults
└── main/
    ├── CMakeLists.txt
    └── main.c
```

## 1) ESP-IDF installeren (eenmalig op Linux)

```bash
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
```

Elke nieuwe shell sessie:

```bash
source ~/esp/esp-idf/export.sh
```

Controle:

```bash
idf.py --version
```

## 2) Project bouwen vanaf command line

```bash
cd /workspace/prompt_engineering/examples/esp32s3_idf_minimal
idf.py set-target esp32s3
idf.py build
```

Wat dit doet:
- target op `esp32s3`
- defaults laden uit `sdkconfig.defaults` (o.a. 16MB flash + PSRAM + W5500)
- compile + link firmware

## 3) Flashen

```bash
idf.py -p /dev/ttyUSB0 flash
```

Als je poort anders is (bijv. `/dev/ttyACM0`), pas de `-p` waarde aan.

## 4) Monitor

```bash
idf.py -p /dev/ttyUSB0 monitor
```

Verwachte log:

```text
I (...) app_main: led_init: ok
I (...) app_main: timer_init: ok
I (...) app_main: eth_w5500_init: ok
I (...) app_main: running: led blink timer=500000 us + W5500 up
```

## Hardware mapping voor W5500

In `main/main.c` staan default pinnen. Pas die aan aan jouw board:

- `g_pin_spi_miso`
- `g_pin_spi_mosi`
- `g_pin_spi_sclk`
- `g_pin_spi_cs`
- `g_pin_eth_int`
- `g_pin_eth_rst`

## Waarom dit minimaal en robuust is

- Platte C met expliciete state (`static` globals)
- Geen app-level heap allocatie in eigen code
- Expliciete tagged-union error returns (`app_status_t`)
- Geen verborgen control flow

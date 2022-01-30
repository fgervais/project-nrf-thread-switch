# 

## Docker environment

```bash
cd application
docker-compose run nrf bash
```

## Build

```bash
cd application
west build -b nrf52840dongle_nrf52840 -s app
```

## menuconfig

```bash
cd application
west build -b nrf52840dongle_nrf52840 -s app -t menuconfig
```

## Flash

```bash
nrfutil pkg generate --hw-version 52 --sd-req=0x00 \
        --application build/zephyr/zephyr.hex \
        --application-version 1 first.zip

nrfutil dfu usb-serial -pkg first.zip -p /dev/ttyACM0
```

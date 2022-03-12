# 

## Init

```bash
mkdir project-nrf-connect-test
cd project-nrf-connect-test
docker run --rm -it -u $(id -u):$(id -g) -v $(pwd):/workdir/project coderbyheart/fw-nrfconnect-nrf-docker:v1.8-branch bash
west init -m https://github.com/fgervais/project-nrf-thread-switch.git .
west update
```

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

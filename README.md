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

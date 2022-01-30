# 

## Docker environment

```bash
docker-compose run nrf bash
```

## Build

```bash
west build -b nrf52840dongle_nrf52840 -s app
```

## menuconfig

```bash
west build -b nrf52840dongle_nrf52840 -s app -t menuconfig
```

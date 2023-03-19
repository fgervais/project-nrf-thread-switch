![Finger](assets/img/finger.jpg)

[Project Management](#project-management)
[Hardware](#hardware)
[Border Router](#border-router)
[Battery Life](#battery-life)

# Project Management

## Init

```bash
mkdir project-nrf-connect-test
cd project-nrf-connect-test
docker run --rm -it -u $(id -u):$(id -g) -v $(pwd):/workdir/project nordicplayground/nrfconnect-sdk:v2.0-branch bash
west init -m https://github.com/fgervais/project-nrf-thread-switch.git .
west update
```

### Add Thread network key

Add `secret.conf` in the `app` folder with content like this which matches your
network key:

```
CONFIG_OPENTHREAD_NETWORKKEY="00:11:22:33:44:55:66:77:88:99:aa:bb:cc:dd:ee:ff"
```

## Docker environment

## Build

```bash
cd application
docker compose run --rm nrf west build -b pink_panda -s app
```

## menuconfig

```bash
cd application
docker compose run --rm nrf west build -b pink_panda -s app -t menuconfig
```

## Clean

```bash
cd application
rm -rf build/
```

## Update

```bash
cd application
docker compose run --rm nrf west update
```

## Flash

### nrfutil

```bash
cd application
docker compose run --rm nrf nrfutil pkg generate \
        --hw-version 52 --sd-req=0x00 \
        --application build/zephyr/zephyr.hex \
        --application-version 1 first.zip

docker compose -f docker-compose.yml -f docker-compose.device.yml run nrf \
        nrfutil dfu usb-serial -pkg first.zip -p /dev/ttyACM0
```

### pyocd
```bash
cd application
pyocd flash -e sector -t nrf52840 -f 4000000 build/zephyr/zephyr.hex
```

# Hardware

![Finger](assets/img/assembled.jpg)

https://github.com/fgervais/project-nrf-thread-switch_hardware

## Border Router

![Border Router](assets/img/border-router.jpg)

### Setup the RCP firmware on an nRF52840 Dongle

```
git clone --recursive https://github.com/openthread/ot-nrf528xx.git
cd ot-nrf528xx

wget https://developer.arm.com/-/media/Files/downloads/gnu-rm/9-2020q2/gcc-arm-none-eabi-9-2020-q2-update-"$(arch)"-linux.tar.bz2
tar xf gcc-arm-none-eabi-9-2020-q2-update-x86_64-linux.tar.bz2

docker run --rm -u $(id -u):$(id -g) -v $(pwd):/workdir/project nordicplayground/nrfconnect-sdk:v2.0-branch \
        bash -c 'PATH=$PATH:/workdir/project/gcc-arm-none-eabi-9-2020-q2-update/bin ./script/build nrf52840 USB_trans -DOT_BOOTLOADER=USB'
docker run --rm -u $(id -u):$(id -g) -v $(pwd):/workdir/project nordicplayground/nrfconnect-sdk:v2.0-branch \
        bash -c 'PATH=$PATH:/workdir/project/gcc-arm-none-eabi-9-2020-q2-update/bin arm-none-eabi-objcopy -O ihex build/bin/ot-rcp build/bin/ot-rcp.hex'
docker run --rm -u $(id -u):$(id -g) -v $(pwd):/workdir/project nordicplayground/nrfconnect-sdk:v2.0-branch \
        nrfutil pkg generate --hw-version 52 --sd-req=0x00 --application build/bin/ot-rcp.hex --application-version 1 build/bin/ot-rcp.zip
docker run --rm -u $(id -u):$(id -g) -v $(pwd):/workdir/project --group-add 20 --device /dev/ttyACM2 --device /dev/bus/usb nordicplayground/nrfconnect-sdk:v2.0-branch \
        nrfutil dfu usb-serial -pkg build/bin/ot-rcp.zip -p /dev/ttyACM2
```

Related documentation: https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/matter/openthread_rcp_nrf_dongle.html

### Start OTBR

```
sudo modprobe ip6table_filter
docker run --sysctl "net.ipv6.conf.all.disable_ipv6=0 net.ipv4.conf.all.forwarding=1 net.ipv6.conf.all.forwarding=1" -p 8080:80 --volume /dev/ttyACM0:/dev/ttyACM0 --privileged openthread/otbr --radio-url spinel+hdlc+uart:///dev/ttyACM0 --nat64-prefix "fd00:64::/96"
```

We use `--nat64-prefix "fd00:64::/96"` so the nat64 will also forward to private
ipv4 addresses.

With the default prefix those forwards are prohibited by [RFC 6052](https://datatracker.ietf.org/doc/html/rfc6052).

# Battery Life

## Power consumption

### Idle

Average: 2.43µA

The child (SED) poll period is set to 236s.

This selection is a full window starting from after the previous poll to after
this last poll.

![Idle Consumption](assets/img/idle-consumption.png)

### Button press

Each press: 2.03mC

![Press Consumption](assets/img/press-consumption.png)

## CR2032 expected life

Based on commit [`9aaf1e6efb23c5ed54ebaed8196d6e84e29ed6b3`](https://github.com/fgervais/project-nrf-thread-switch/tree/9aaf1e6efb23c5ed54ebaed8196d6e84e29ed6b3/app)

Energizer CR2032 = $810 \\, C$

### Idle all the time

```math
\frac{810 \, C}{2.43 \times 10^-6 \, \frac{C}{s}} = 333.33 \times 10^6 \, s
```

```math
333.33 \times 10^6 \, seconds \times \frac{1 \, minute}{60 \, second} \times \frac{1 \, hour}{60 \, minute} \times \frac{1 \, day}{24 \, hour} \times \frac{1 \, year}{365.25 \, days} = 10.56 \, years
```

### 50 presses per day

Idle consumption per day:
```math
2.43 \times 10^-6 \, \frac{C}{s} \times (60 \times 60 \times 24) = 209.95 \times 10^-3 \frac{C}{day}
```

Press consumption per day:
```math
2.03 \times 10^-3 \, C \times 50 = 101.5 \times 10^-3 \, C
```

Total per day:
```math
209.95 \times 10^-3 \frac{C}{day} + 101.5 \times 10^-3 \, \frac{C}{day} = 311.45 \times 10^-3 \, \frac{C}{day}
```

Runtime:
```math
810 \, C \div 311.45 \times 10^-3 \, \frac{C}{day} = 2600.72 \, days = 7.12 \, years
```

Note: We did not substract the press time from the idle time. It will affect 
negatively the calculated expected runtime but it should be negligible.
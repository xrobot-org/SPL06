# SPL06

XRobot Module for the Goertek SPL06 barometric pressure sensor (SPI).

The constructor configures the SPI (CPOL high, second edge, prescaler
`DIV_4`), checks the product ID (`0x10`), reads the calibration coefficients,
sets pressure to 128 Hz with 16x oversampling and temperature to 8 Hz with 8x
oversampling, and starts continuous pressure and temperature measurement. A
wrong product ID or a failed SPI configuration stops with `ASSERT`.

The `spl06_thread` thread (`HIGH` priority) reads temperature and pressure,
compensates them with the calibration coefficients and publishes the result
every `sample_period_ms`. `height_cm` is an empirical estimate from the
difference to 101400 Pa: `0.82 * (dp / 1000)^3 + 9 * dp` with
`dp = 101400 - pressure_pa`.

The module drives no chip-select pin. The `spi` object passed in must select
the SPL06 itself; if the sensor shares a physical SPI bus with other devices,
chip-select handling and bus locking belong in that object.

`OnMonitor()` logs a warning when any output is NaN.

## Published topic

`data_topic_name` (default `spl06_data`), type `SPL06::Data`:

| Field | Meaning |
| --- | --- |
| `temperature_c` | temperature, °C |
| `pressure_pa` | pressure, Pa |
| `height_cm` | estimated height, cm |

## Shell command

The module adds the command `spl06` to `ramfs`:

```sh
spl06 show <time_ms> <interval_ms>   # print pressure, temperature and height (interval clamped to 10..1000 ms)
```

## Dependencies

No other Modules; LibXR only.

## Constructor

```cpp
SPL06(LibXR::SPI& spi, LibXR::RamFS& ramfs,
      const char* data_topic_name = "spl06_data",
      uint32_t sample_period_ms = 50,
      size_t task_stack_depth = 1024);
```

Dependencies:

- `spi`: SPI device handle for the SPL06 (see the chip-select note above).
- `ramfs`: RamFS that receives the `spl06` command.

Configuration:

- `data_topic_name`: name of the published topic, default `spl06_data`.
- `sample_period_ms`: sleep between two samples, ms, default 50.
- `task_stack_depth`: stack size of the sampling thread, default 1024.

## Use

```sh
xrobot module add xrobot-org/SPL06
xrobot setup
xrobot instance add xrobot-org/SPL06
```

`xrobot instance add` writes an instance to `User/xrobot.yaml` with empty
dependencies and the source defaults; set the dependencies to the names of
objects the BSP registers with `XR_REGISTER`:

```yaml
modules:
  - module: xrobot-org/SPL06
    id: spl06_0
    args:
      - spi: spl06_spi
      - ramfs: ramfs
      - data_topic_name: '"spl06_data"'
      - sample_period_ms: '50'
      - task_stack_depth: '1024'
```

BSP side:

```cpp
XR_REGISTER(spl06_spi, LibXR::SPI);
XR_REGISTER(ramfs, LibXR::RamFS);
```

Run `xrobot setup` again to generate `User/xrobot_main.hpp`.

`xrobot module show .` in this repository, or
`xrobot module show Modules/xrobot-org/SPL06` in a BSP, prints the current
constructor.

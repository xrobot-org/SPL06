# SPL06

Goertek SPL06 SPI barometric pressure sensor module for XRobot.

This module initializes the SPL06 over a device-level SPI handle, loads pressure
and temperature calibration coefficients, starts continuous conversion, samples
pressure / temperature / estimated height in a background thread, and exposes a
RamFS shell command for status output.

The `spl06_spi` handle is expected to be prepared by the User layer. If the
sensor shares a physical SPI bus with other devices, chip-select handling and bus
locking should be encapsulated in that handle.

## Required Hardware

- `spl06_spi`
- `ramfs`

## Constructor Arguments

- `data_topic_name`: default `"spl06_data"`
- `sample_period_ms`: default `50`
- `task_stack_depth`: default `1024`

## Published Topics

- `data_topic_name`: `SPL06::Data`, pressure in Pa, temperature in degrees C, and estimated height in cm

## Shell Commands

The module registers `spl06` in `RamFS`.

- `spl06` or `spl06 status`: print latest pressure, temperature, and height

## XRobot Configuration Example

```yaml
- id: barometer
  name: SPL06
  constructor_args:
    data_topic_name: "spl06_data"
    sample_period_ms: 50
    task_stack_depth: 1024
```

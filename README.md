# SPL06

Goertek SPL06 气压传感器驱动模块 / Driver Module for the Goertek SPL06 barometric pressure sensor

## 1. 模块作用 / Purpose

构造时，SPL06 配置 SPI（CPOL 为高、第二个边沿采样、分频 `DIV_4`），检查产品 ID（`0x10`），读取校准系数，把气压设为 128 Hz、16 倍过采样，温度设为 8 Hz、8 倍过采样，并启动气压与温度的连续测量。产品 ID 与 SPI 配置结果由 `ASSERT` 检查，Debug 构建中检查失败时进入 LibXR 致命错误处理。

随后创建线程 `spl06_thread`（`HIGH` 优先级，栈深 `task_stack_depth`），读取温度与气压，用校准系数补偿，每隔 `sample_period_ms` 发布一次结果。`height_cm` 是相对 101400 Pa 的经验估算：`0.82 * (dp / 1000)^3 + 9 * dp`，其中 `dp = 101400 - pressure_pa`。

`spi` 对象负责选中 SPL06；传感器与其他设备共用同一条物理 SPI 总线时，片选和总线互斥由该对象处理。

`OnMonitor()` 在任一输出为 NaN 时输出警告。

Upon construction, SPL06 configures the SPI (CPOL high, second edge, prescaler `DIV_4`), checks the product ID (`0x10`), reads the calibration coefficients, sets the pressure to 128 Hz with 16x oversampling and the temperature to 8 Hz with 8x oversampling, and starts continuous pressure and temperature measurement. The product ID and the SPI configuration result are checked with `ASSERT`; in Debug builds a failed check enters the LibXR fatal error handler.

It then creates the thread `spl06_thread` (`HIGH` priority, stack depth `task_stack_depth`), which reads the temperature and pressure, compensates them with the calibration coefficients and publishes the result every `sample_period_ms`. `height_cm` is an empirical estimate relative to 101400 Pa: `0.82 * (dp / 1000)^3 + 9 * dp` with `dp = 101400 - pressure_pa`.

The `spi` object selects the SPL06; when the sensor shares a physical SPI bus with other devices, chip-select handling and bus locking are done by that object.

`OnMonitor()` logs a warning when any output is NaN.

## 2. Shell 命令 / Shell Command

模块向 `ramfs` 添加命令 `spl06`：

- `spl06`：打印用法。
- `spl06 show <time_ms> <interval_ms>`：打印气压、温度和高度，`interval_ms` 限制在 10 到 1000 ms。

The Module adds the command `spl06` to `ramfs`:

- `spl06`: print the usage.
- `spl06 show <time_ms> <interval_ms>`: print the pressure, temperature and height, with `interval_ms` clamped to 10 to 1000 ms.

## 3. 构造接口 / Constructor

```cpp
SPL06(LibXR::SPI& spi, LibXR::RamFS& ramfs,
      const char* data_topic_name = "spl06_data",
      uint32_t sample_period_ms = 50,
      size_t task_stack_depth = 1024);
```

依赖：

- `spi`：选中 SPL06 的 SPI 设备句柄。
- `ramfs`：接收 `spl06` 命令的 RamFS。

配置参数：

- `data_topic_name`：发布的 Topic 名称，默认 `spl06_data`。
- `sample_period_ms`：两次采样之间的休眠时间，单位 ms，默认 50。
- `task_stack_depth`：采样线程栈深，单位字节，默认 1024。

Dependencies:

- `spi`: the SPI device handle that selects the SPL06.
- `ramfs`: the RamFS that receives the `spl06` command.

Configuration parameters:

- `data_topic_name`: name of the published Topic, default `spl06_data`.
- `sample_period_ms`: sleep between two samples in ms, default 50.
- `task_stack_depth`: stack depth of the sampling thread in bytes, default 1024.

## 4. Topic

| Topic | 方向 | 类型 | 说明 |
| --- | --- | --- | --- |
| `data_topic_name`（默认 `spl06_data`） | 发布 | `SPL06::Data` | `temperature_c`：温度，°C；`pressure_pa`：气压，Pa；`height_cm`：估算高度，cm |

| Topic | Direction | Type | Meaning |
| --- | --- | --- | --- |
| `data_topic_name` (default `spl06_data`) | Publish | `SPL06::Data` | `temperature_c`: temperature in °C; `pressure_pa`: pressure in Pa; `height_cm`: estimated height in cm |

## 5. 配置示例 / Configuration Example

`xrobot instance add xrobot-org/SPL06` 写入的实例，`spi` 与 `ramfs` 填写为 BSP 通过 `XR_REGISTER`（硬件注册）注册的名称：

An instance written by `xrobot instance add xrobot-org/SPL06`, with `spi` and `ramfs` set to names registered by the BSP with `XR_REGISTER` (Registration):

```yaml
modules:
  - module: xrobot-org/SPL06
    id: spl06_0
    args:
      - spi: spl06_spi
      - ramfs: ramfs
      - data_topic_name: "spl06_data"
      - sample_period_ms: 50
      - task_stack_depth: 1024
```

## 6. 依赖与硬件 / Dependencies and Hardware

依赖：LibXR。

硬件：一片 SPL06 气压传感器，通过 SPI 连接（CPOL 为高，第二个边沿采样）。

Dependencies: LibXR.

Hardware: one SPL06 barometric pressure sensor on SPI (CPOL high, second-edge sampling).

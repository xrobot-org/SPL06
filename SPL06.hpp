#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: SPL06 barometer driver module
constructor_args:
  - data_topic_name: "spl06_data"
  - sample_period_ms: 50
  - task_stack_depth: 1024
template_args: []
required_hardware:
  - spi_spl06/spi2/SPI2
  - spl06_cs
  - spi2_mutex
  - ramfs
depends: []
=== END MANIFEST === */
// clang-format on

#include "app_framework.hpp"
#include "gpio.hpp"
#include "logger.hpp"
#include "message.hpp"
#include "mutex.hpp"
#include "ramfs.hpp"
#include "spi.hpp"
#include "thread.hpp"
#include "timebase.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

class SPL06 : public LibXR::Application {
 public:
  struct Data {
    float temperature_c = 0.0f;
    float pressure_pa = 0.0f;
    float height_cm = 0.0f;
  };

  struct Calibration {
    int16_t c0 = 0;
    int16_t c1 = 0;
    int32_t c00 = 0;
    int32_t c10 = 0;
    int16_t c01 = 0;
    int16_t c11 = 0;
    int16_t c20 = 0;
    int16_t c21 = 0;
    int16_t c30 = 0;
  };

  class OptionalBusLock {
   public:
    explicit OptionalBusLock(LibXR::Mutex* mutex) : mutex_(mutex) {
      if (mutex_ != nullptr) {
        mutex_->Lock();
      }
    }

    ~OptionalBusLock() {
      if (mutex_ != nullptr) {
        mutex_->Unlock();
      }
    }

   private:
    LibXR::Mutex* mutex_;
  };

  SPL06(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app,
        const char* data_topic_name, uint32_t sample_period_ms,
        size_t task_stack_depth)
      : sample_period_ms_(sample_period_ms),
        topic_(LibXR::Topic::CreateTopic<Data>(data_topic_name)),
        cs_(hw.template FindOrExit<LibXR::GPIO>({"spl06_cs"})),
        spi_(hw.template FindOrExit<LibXR::SPI>({"spi_spl06", "spi2", "SPI2"})),
        spi_mutex_(hw.template Find<LibXR::Mutex>({"spi2_mutex"})),
        op_spi_(sem_spi_),
        cmd_file_(LibXR::RamFS::CreateFile("spl06", CommandFunc, this)) {
    app.Register(*this);
    hw.template FindOrExit<LibXR::RamFS>({"ramfs"})->Add(cmd_file_);

    ASSERT(spi_->SetConfig({.clock_polarity = LibXR::SPI::ClockPolarity::HIGH,
                            .clock_phase = LibXR::SPI::ClockPhase::EDGE_2,
                            .prescaler = LibXR::SPI::Prescaler::DIV_4}) ==
           LibXR::ErrorCode::OK);

    cs_->SetConfig({.direction = LibXR::GPIO::Direction::OUTPUT_PUSH_PULL,
                    .pull = LibXR::GPIO::Pull::NONE});
    cs_->Write(true);

    chip_id_ = ReadReg(REG_PRODUCT_ID);
    ASSERT(chip_id_ == 0x10);

    ReadCalibration();
    RateSet(0, 128, 16);
    RateSet(1, 8, 8);
    StartContinuous(3);

    XR_LOG_PASS("SPL06: Init succeeded.");

    thread_.Create(this, ThreadFunc, "spl06_thread", task_stack_depth,
                   LibXR::Thread::Priority::HIGH);
  }

  void OnMonitor() override {
    if (std::isnan(data_.pressure_pa) || std::isnan(data_.temperature_c) ||
        std::isnan(data_.height_cm)) {
      XR_LOG_WARN("SPL06: NaN data detected.");
    }
  }

 private:
  static constexpr uint8_t REG_PSR_B2 = 0x00;
  static constexpr uint8_t REG_TMP_B2 = 0x03;
  static constexpr uint8_t REG_PRS_CFG = 0x06;
  static constexpr uint8_t REG_TMP_CFG = 0x07;
  static constexpr uint8_t REG_MEAS_CFG = 0x08;
  static constexpr uint8_t REG_CFG_REG = 0x09;
  static constexpr uint8_t REG_PRODUCT_ID = 0x0D;

  void WriteReg(uint8_t reg, uint8_t value) {
    OptionalBusLock lock(spi_mutex_);
    cs_->Write(false);
    spi_->MemWrite(reg, value, op_spi_);
    cs_->Write(true);
  }

  uint8_t ReadReg(uint8_t reg) {
    OptionalBusLock lock(spi_mutex_);
    uint8_t value = 0;
    cs_->Write(false);
    spi_->MemRead(reg, {&value, 1}, op_spi_);
    cs_->Write(true);
    return value;
  }

  void ReadRegs(uint8_t reg, uint8_t* data, size_t size) {
    OptionalBusLock lock(spi_mutex_);
    cs_->Write(false);
    spi_->MemRead(reg, {data, size}, op_spi_);
    cs_->Write(true);
  }

  static int32_t SignExtend12(uint32_t value) {
    return (value & 0x800) ? static_cast<int32_t>(value | 0xFFFFF000u)
                           : static_cast<int32_t>(value);
  }

  static int32_t SignExtend20(uint32_t value) {
    return (value & 0x80000) ? static_cast<int32_t>(value | 0xFFF00000u)
                             : static_cast<int32_t>(value);
  }

  static int32_t SignExtend24(uint32_t value) {
    return (value & 0x800000) ? static_cast<int32_t>(value | 0xFF000000u)
                              : static_cast<int32_t>(value);
  }

  void ReadCalibration() {
    calibration_.c0 =
        SignExtend12((static_cast<uint32_t>(ReadReg(0x10)) << 4) | (ReadReg(0x11) >> 4));
    calibration_.c1 = SignExtend12(
        ((static_cast<uint32_t>(ReadReg(0x11)) & 0x0F) << 8) | ReadReg(0x12));
    calibration_.c00 = SignExtend20((static_cast<uint32_t>(ReadReg(0x13)) << 12) |
                                    (static_cast<uint32_t>(ReadReg(0x14)) << 4) |
                                    (ReadReg(0x15) >> 4));
    calibration_.c10 = SignExtend20((static_cast<uint32_t>(ReadReg(0x15)) << 16) |
                                    (static_cast<uint32_t>(ReadReg(0x16)) << 8) |
                                    ReadReg(0x17));
    calibration_.c01 =
        static_cast<int16_t>((static_cast<uint32_t>(ReadReg(0x18)) << 8) | ReadReg(0x19));
    calibration_.c11 =
        static_cast<int16_t>((static_cast<uint32_t>(ReadReg(0x1A)) << 8) | ReadReg(0x1B));
    calibration_.c20 =
        static_cast<int16_t>((static_cast<uint32_t>(ReadReg(0x1C)) << 8) | ReadReg(0x1D));
    calibration_.c21 =
        static_cast<int16_t>((static_cast<uint32_t>(ReadReg(0x1E)) << 8) | ReadReg(0x1F));
    calibration_.c30 =
        static_cast<int16_t>((static_cast<uint32_t>(ReadReg(0x20)) << 8) | ReadReg(0x21));
  }

  void RateSet(uint8_t sensor, uint8_t sample_rate, uint8_t over_sample) {
    uint8_t reg = 0;
    int32_t k = 524288;

    switch (sample_rate) {
      case 2:
        reg |= (1u << 4);
        break;
      case 4:
        reg |= (2u << 4);
        break;
      case 8:
        reg |= (3u << 4);
        break;
      case 16:
        reg |= (4u << 4);
        break;
      case 32:
        reg |= (5u << 4);
        break;
      case 64:
        reg |= (6u << 4);
        break;
      case 128:
        reg |= (7u << 4);
        break;
      default:
        break;
    }

    switch (over_sample) {
      case 2:
        reg |= 1u;
        k = 1572864;
        break;
      case 4:
        reg |= 2u;
        k = 3670016;
        break;
      case 8:
        reg |= 3u;
        k = 7864320;
        break;
      case 16:
        reg |= 4u;
        k = 253952;
        break;
      case 32:
        reg |= 5u;
        k = 516096;
        break;
      case 64:
        reg |= 6u;
        k = 1040384;
        break;
      case 128:
        reg |= 7u;
        k = 2088960;
        break;
      default:
        break;
    }

    if (sensor == 0) {
      kp_ = static_cast<float>(k);
      WriteReg(REG_PRS_CFG, reg);
      if (over_sample > 8) {
        WriteReg(REG_CFG_REG, ReadReg(REG_CFG_REG) | 0x04);
      }
    } else {
      kt_ = static_cast<float>(k);
      WriteReg(REG_TMP_CFG, reg | 0x80);
      if (over_sample > 8) {
        WriteReg(REG_CFG_REG, ReadReg(REG_CFG_REG) | 0x08);
      }
    }
  }

  void StartContinuous(uint8_t mode) { WriteReg(REG_MEAS_CFG, mode + 4); }

  int32_t ReadRawPressure() {
    uint8_t raw[3] = {0};
    // Align legacy ANO_PioneerPro-088 exactly: read each byte with an
    // independent SPI register transaction instead of a burst read.
    raw[0] = ReadReg(REG_PSR_B2 + 0);
    raw[1] = ReadReg(REG_PSR_B2 + 1);
    raw[2] = ReadReg(REG_PSR_B2 + 2);
    return SignExtend24((static_cast<uint32_t>(raw[0]) << 16) |
                        (static_cast<uint32_t>(raw[1]) << 8) | raw[2]);
  }

  int32_t ReadRawTemperature() {
    uint8_t raw[3] = {0};
    raw[0] = ReadReg(REG_TMP_B2 + 0);
    raw[1] = ReadReg(REG_TMP_B2 + 1);
    raw[2] = ReadReg(REG_TMP_B2 + 2);
    return SignExtend24((static_cast<uint32_t>(raw[0]) << 16) |
                        (static_cast<uint32_t>(raw[1]) << 8) | raw[2]);
  }

  float CalculateTemperature(int32_t raw_temperature) const {
    float t_sc = static_cast<float>(raw_temperature) / kt_;
    return static_cast<float>(calibration_.c0) * 0.5f +
           static_cast<float>(calibration_.c1) * t_sc;
  }

  float CalculatePressure(int32_t raw_pressure, int32_t raw_temperature) const {
    float t_sc = static_cast<float>(raw_temperature) / kt_;
    float p_sc = static_cast<float>(raw_pressure) / kp_;
    float qua2 = static_cast<float>(calibration_.c10) +
                 p_sc * (static_cast<float>(calibration_.c20) +
                         p_sc * static_cast<float>(calibration_.c30));
    float qua3 = t_sc * p_sc *
                 (static_cast<float>(calibration_.c11) +
                  p_sc * static_cast<float>(calibration_.c21));
    return static_cast<float>(calibration_.c00) + p_sc * qua2 +
           t_sc * static_cast<float>(calibration_.c01) + qua3;
  }

  void Update() {
    int32_t raw_temperature = ReadRawTemperature();
    int32_t raw_pressure = ReadRawPressure();
    data_.temperature_c = CalculateTemperature(raw_temperature);
    data_.pressure_pa = CalculatePressure(raw_pressure, raw_temperature);
    float alt_3 = (101400.0f - data_.pressure_pa) / 1000.0f;
    data_.height_cm = 0.82f * alt_3 * alt_3 * alt_3 +
                      9.0f * (101400.0f - data_.pressure_pa);
  }

  static void ThreadFunc(SPL06* spl06) {
    while (true) {
      spl06->Update();
      spl06->topic_.Publish(spl06->data_);
      LibXR::Thread::Sleep(spl06->sample_period_ms_);
    }
  }

  static int CommandFunc(SPL06* spl06, int argc, char** argv) {
    if (argc == 1) {
      LibXR::STDIO::Printf<"Usage:\r\n">();
      LibXR::STDIO::Printf<
          "  show [time_ms] [interval_ms] - Print pressure, temperature and height.\r\n">();
      return 0;
    }

    if (argc == 4 && std::strcmp(argv[1], "show") == 0) {
      int time_ms = std::atoi(argv[2]);
      int interval_ms = std::atoi(argv[3]);
      interval_ms = std::clamp(interval_ms, 10, 1000);

      while (time_ms > 0) {
        LibXR::STDIO::Printf<"SPL06: pressure=%fPa temp=%fC height=%fcm\r\n">(
            spl06->data_.pressure_pa, spl06->data_.temperature_c,
            spl06->data_.height_cm);
        LibXR::Thread::Sleep(interval_ms);
        time_ms -= interval_ms;
      }
      return 0;
    }

    LibXR::STDIO::Printf<"Error: Invalid arguments.\r\n">();
    return -1;
  }

  uint32_t sample_period_ms_ = 50;
  uint8_t chip_id_ = 0;
  float kp_ = 524288.0f;
  float kt_ = 524288.0f;
  Calibration calibration_;
  Data data_;
  LibXR::Topic topic_;
  LibXR::GPIO* cs_;
  LibXR::SPI* spi_;
  LibXR::Mutex* spi_mutex_ = nullptr;
  LibXR::Semaphore sem_spi_;
  LibXR::SPI::OperationRW op_spi_;
  LibXR::RamFS::File cmd_file_;
  LibXR::Thread thread_;
};

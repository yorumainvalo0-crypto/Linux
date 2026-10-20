// MiniArcade Color: MPU6050 driver (see gyro.h).
#include "gyro.h"
#include "driver/i2c_master.h"

static i2c_master_bus_handle_t bus = NULL;
static i2c_master_dev_handle_t dev = NULL;

static bool reg(uint8_t r, uint8_t v) {
  uint8_t b[2] = { r, v };
  return i2c_master_transmit(dev, b, 2, 20) == ESP_OK;
}

void gyroClose() {
  if (dev) i2c_master_bus_rm_device(dev);
  if (bus) i2c_del_master_bus(bus);
  dev = NULL;
  bus = NULL;
}

bool gyroOpen(int8_t sda, int8_t scl) {
  gyroClose();
  if (sda < 0 || scl < 0 || sda == scl) return false;
  i2c_master_bus_config_t c = {};
  c.i2c_port = -1;                                   // any free controller
  c.sda_io_num = (gpio_num_t)sda;
  c.scl_io_num = (gpio_num_t)scl;
  c.clk_source = I2C_CLK_SRC_DEFAULT;
  c.glitch_ignore_cnt = 7;
  c.flags.enable_internal_pullup = true;             // the GY-521 has its own, this is spare
  if (i2c_new_master_bus(&c, &bus) != ESP_OK) { bus = NULL; return false; }
  uint8_t addr = 0;
  if (i2c_master_probe(bus, 0x68, 20) == ESP_OK) addr = 0x68;        // AD0 low
  else if (i2c_master_probe(bus, 0x69, 20) == ESP_OK) addr = 0x69;   // AD0 high
  if (!addr) { gyroClose(); return false; }
  i2c_device_config_t d = {};
  d.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  d.device_address = addr;
  d.scl_speed_hz = 400000;
  if (i2c_master_bus_add_device(bus, &d, &dev) != ESP_OK) { dev = NULL; gyroClose(); return false; }
  uint8_t who = 0, r = 0x75;                         // WHO_AM_I
  if (i2c_master_transmit_receive(dev, &r, 1, &who, 1, 20) != ESP_OK || who == 0 || who == 0xFF) { gyroClose(); return false; }
  bool ok = reg(0x6B, 0x00)                          // wake up
         && reg(0x1A, 0x03)                          // low-pass 44 Hz: no jitter from the keys
         && reg(0x1C, 0x00);                         // +-2 g
  if (!ok) gyroClose();
  return ok;
}

bool gyroAccel(int16_t a[3]) {
  if (!dev) return false;
  uint8_t r = 0x3B, b[6];
  if (i2c_master_transmit_receive(dev, &r, 1, b, 6, 20) != ESP_OK) return false;
  for (int i = 0; i < 3; i++) a[i] = (int16_t)(b[2 * i] << 8 | b[2 * i + 1]);
  return true;
}

#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>

class ST7305_U8g2 {
 private:
  int _sck, _mosi, _dc, _cs, _rst;
  SPIClass *_spi = nullptr;
  U8G2 u8g2_wrapper;
  uint8_t *_my_buf = nullptr;
  void _cmd(uint8_t cmd);
  void _cmd_data(uint8_t cmd, const uint8_t *data, size_t len);
  static uint8_t u8x8_d_st7305_custom(u8x8_t *, uint8_t, uint8_t, void *);
  static uint8_t u8x8_byte_custom(u8x8_t *, uint8_t, uint8_t, void *);

 public:
  ST7305_U8g2(int sck = 11, int mosi = 12, int dc = 5, int cs = 40, int rst = 41);
  ~ST7305_U8g2();
  void begin(const u8g2_cb_t *rotation = U8G2_R1);
  void reset();
  void fullInit();
  U8G2 *getU8g2() { return &u8g2_wrapper; }
};

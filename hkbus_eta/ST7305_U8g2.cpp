#include "ST7305_U8g2.h"

#define SPI_CLK 24000000
#define TILE_WIDTH 38
#define TILE_HEIGHT 50
#define BUFFER_ROW_BYTES (TILE_WIDTH * 8)

static ST7305_U8g2 *instance = nullptr;
static u8x8_display_info_t display_info = {
  0, 1, 0, 0, 20, 50, 0, 0, SPI_CLK, 0, 4, 0, 0,
  TILE_WIDTH, TILE_HEIGHT, 0, 0, 300, 400
};

ST7305_U8g2::ST7305_U8g2(int sck, int mosi, int dc, int cs, int rst)
    : _sck(sck), _mosi(mosi), _dc(dc), _cs(cs), _rst(rst) {
  _spi = new SPIClass(HSPI);
  instance = this;
}

ST7305_U8g2::~ST7305_U8g2() {
  if (_spi) { _spi->end(); delete _spi; }
  if (_my_buf) free(_my_buf);
  if (instance == this) instance = nullptr;
}

void ST7305_U8g2::_cmd(uint8_t cmd) {
  digitalWrite(_dc, LOW); digitalWrite(_cs, LOW);
  _spi->transfer(cmd);
  digitalWrite(_cs, HIGH);
}

void ST7305_U8g2::_cmd_data(uint8_t cmd, const uint8_t *data, size_t len) {
  digitalWrite(_dc, LOW); digitalWrite(_cs, LOW);
  _spi->transfer(cmd);
  if (len) {
    digitalWrite(_dc, HIGH);
    _spi->transferBytes((uint8_t *)data, nullptr, len);
  }
  digitalWrite(_cs, HIGH);
}

void ST7305_U8g2::reset() {
  digitalWrite(_rst, HIGH); delay(50);
  digitalWrite(_rst, LOW); delay(20);
  digitalWrite(_rst, HIGH); delay(50);
}

uint8_t ST7305_U8g2::u8x8_byte_custom(u8x8_t *, uint8_t, uint8_t, void *) {
  return 1;
}

uint8_t ST7305_U8g2::u8x8_d_st7305_custom(
    u8x8_t *u8x8, uint8_t msg, uint8_t, void *arg_ptr) {
  if (!instance) return 0;
  switch (msg) {
    case U8X8_MSG_DISPLAY_SETUP_MEMORY:
      u8x8_d_helper_display_setup_memory(u8x8, &display_info);
      break;
    case U8X8_MSG_DISPLAY_INIT:
      instance->fullInit();
      break;
    case U8X8_MSG_DISPLAY_DRAW_TILE: {
      u8x8_tile_t *tile = (u8x8_tile_t *)arg_ptr;
      int first = tile->x_pos * 8;
      int last = min(299, (tile->x_pos + tile->cnt) * 8 - 1);
      int addr_start = 0x12 + first / 12;
      int addr_end = 0x12 + last / 12;
      int send_start = (addr_start - 0x12) * 3;
      int send_count = (addr_end - addr_start + 1) * 3;
      int first_col = (addr_start - 0x12) * 12;
      int last_col = min(299, (addr_end - 0x12) * 12 + 11);
      uint8_t *row = tile->tile_ptr - ((uint16_t)tile->x_pos * 8U);
      uint8_t columns[] = {(uint8_t)(0x3C - addr_end), (uint8_t)(0x3C - addr_start)};
      instance->_cmd_data(0x2A, columns, sizeof(columns));
      uint8_t rows[] = {(uint8_t)(tile->y_pos * 4), (uint8_t)(tile->y_pos * 4 + 3)};
      instance->_cmd_data(0x2B, rows, sizeof(rows));
      static const uint8_t lut[4][4] = {
        {0x00, 0x80, 0x40, 0xC0}, {0x00, 0x20, 0x10, 0x30},
        {0x00, 0x08, 0x04, 0x0C}, {0x00, 0x02, 0x01, 0x03}
      };
      uint8_t output[300] = {0};
      for (int sr = 0; sr < 4; ++sr) {
        int idx = sr * send_count + (first_col >> 2) - send_start;
        for (int col = first_col; col <= last_col; col += 4, ++idx) {
          output[idx] = lut[0][(row[col] >> (sr * 2)) & 3]
                      | lut[1][(row[col + 1] >> (sr * 2)) & 3]
                      | lut[2][(row[col + 2] >> (sr * 2)) & 3]
                      | lut[3][(row[col + 3] >> (sr * 2)) & 3];
        }
      }
      instance->_cmd_data(0x2C, output, send_count * 4U);
      break;
    }
    default: return 0;
  }
  return 1;
}

void ST7305_U8g2::begin(const u8g2_cb_t *rotation) {
  pinMode(_dc, OUTPUT); pinMode(_cs, OUTPUT); pinMode(_rst, OUTPUT);
  digitalWrite(_cs, HIGH); digitalWrite(_dc, HIGH); digitalWrite(_rst, HIGH);
  _spi->begin(_sck, -1, _mosi, -1);
  _spi->beginTransaction(SPISettings(SPI_CLK, MSBFIRST, SPI_MODE0));
  u8g2_t *u = u8g2_wrapper.getU8g2();
  u8x8_Setup(u8g2_GetU8x8(u), u8x8_d_st7305_custom, u8x8_dummy_cb,
             u8x8_byte_custom, u8x8_dummy_cb);
  _my_buf = (uint8_t *)malloc(BUFFER_ROW_BYTES * TILE_HEIGHT);
  if (!_my_buf) return;
  memset(_my_buf, 0, BUFFER_ROW_BYTES * TILE_HEIGHT);
  u8g2_SetupBuffer(u, _my_buf, TILE_HEIGHT, u8g2_ll_hvline_vertical_top_lsb, rotation);
  u8g2_InitDisplay(u);
  u8g2_SetPowerSave(u, 0);
}

void ST7305_U8g2::fullInit() {
  reset();
  const uint8_t d6[]={0x17,0x02}, d1[]={0x01}, c0[]={0x11,0x04};
  const uint8_t c1[]={0x69,0x69,0x69,0x69}, c2[]={0x19,0x19,0x19,0x19};
  const uint8_t c4[]={0x4B,0x4B,0x4B,0x4B}, d8[]={0x80,0xE9}, b2[]={0x02};
  const uint8_t b3[]={0xE5,0xF6,0x05,0x46,0x77,0x77,0x77,0x77,0x76,0x45};
  const uint8_t b4[]={0x05,0x46,0x77,0x77,0x77,0x77,0x76,0x45};
  const uint8_t timing[]={0x32,0x03,0x1F}, b7[]={0x13}, b0[]={0x64};
  const uint8_t c9[]={0x00}, m36[]={0x48}, m3a[]={0x11}, b9[]={0x20};
  const uint8_t b8[]={0x29}, win_a[]={0x12,0x2A}, win_b[]={0x00,0xC7};
  const uint8_t m35[]={0x00}, d0[]={0xFF};
  _cmd_data(0xD6,d6,sizeof(d6)); _cmd_data(0xD1,d1,sizeof(d1));
  _cmd_data(0xC0,c0,sizeof(c0)); _cmd_data(0xC1,c1,sizeof(c1));
  _cmd_data(0xC2,c2,sizeof(c2)); _cmd_data(0xC4,c4,sizeof(c4));
  _cmd_data(0xC5,c2,sizeof(c2)); _cmd_data(0xD8,d8,sizeof(d8));
  _cmd_data(0xB2,b2,sizeof(b2)); _cmd_data(0xB3,b3,sizeof(b3));
  _cmd_data(0xB4,b4,sizeof(b4)); _cmd_data(0x62,timing,sizeof(timing));
  _cmd_data(0xB7,b7,sizeof(b7)); _cmd_data(0xB0,b0,sizeof(b0));
  _cmd(0x11); delay(120);
  _cmd_data(0xC9,c9,sizeof(c9)); _cmd_data(0x36,m36,sizeof(m36));
  _cmd_data(0x3A,m3a,sizeof(m3a)); _cmd_data(0xB9,b9,sizeof(b9));
  // 0x20 = normal polarity (white background, black pixels).
  // 0x21 enabled display inversion and produced a black background.
  _cmd_data(0xB8,b8,sizeof(b8)); _cmd(0x20);
  _cmd_data(0x2A,win_a,sizeof(win_a)); _cmd_data(0x2B,win_b,sizeof(win_b));
  _cmd_data(0x35,m35,sizeof(m35)); _cmd_data(0xD0,d0,sizeof(d0));
  _cmd(0x38); _cmd(0x29);
}

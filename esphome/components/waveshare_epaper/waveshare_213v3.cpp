#include "waveshare_epaper.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace waveshare_epaper {

static const char *const TAG = "waveshare_2.13v3";

static const uint8_t PARTIAL_LUT[] = {
  0x32, // cmd
  0x0, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x40, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0xF, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0, 0x0, 0x0,
};

static const uint8_t FULL_LUT[] = {
  0x32,
  0x80, 0x4A, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x40, 0x4A, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x80, 0x4A, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x40, 0x4A, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0xF, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0xF, 0x0, 0x0, 0xF, 0x0, 0x0, 0x2,
  0xF, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0, 0x0, 0x0,
};

static const uint8_t WF_PARTIAL_2IN13_V3[159] =
  {
    0x0, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x80, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x40, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x14, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0, 0x0, 0x0,
    0x22, 0x17, 0x41, 0x00, 0x32, 0x36,
  };

static const uint8_t WS_20_30_2IN13_V3[159] =
  {
    0x80, 0x4A, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x40, 0x4A, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x80, 0x4A, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x40, 0x4A, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xF, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xF, 0x0, 0x0, 0xF, 0x0, 0x0, 0x2,
    0xF, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0, 0x0, 0x0,
    0x22, 0x17, 0x41, 0x0, 0x32, 0x36
  };

static const uint8_t ACTIVATE = 0x20;
static const uint8_t WRITE_BUFFER = 0x24;
static const uint8_t ON_FULL[] = {0x22, 0xC7};
static const uint8_t ON_PARTIAL[] = {0x22, 0x0F};

static const uint8_t CMD1[] = {0x3F, 0x22};
static const uint8_t GATEV[] = {0x03, 0x17};
static const uint8_t SRCV[] = {0x04, 0x41, 0x00, 0x32};
static const uint8_t VCOM[] = {0x2C, 0x36};
static const uint8_t CMD5[] = {0x37, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00};
static const uint8_t UPSEQ[] = {0x22, 0xC0};

static const uint8_t INIT1[] = {0x01, 0xF9, 0x00, 0x00};  // driver output control
static const uint8_t INIT2[] = {0x11, 0x03};              // data entry mode
static const uint8_t INIT3[] = {0x21, 0x00, 0x80};        // Display update control
static const uint8_t INIT4[] = {0x18, 0x80};              // Temp sensor

static const uint8_t RAM_X_START[] = {0x44, 0x00, 121 / 8};     // set ram_x_address_start_end
static const uint8_t RAM_Y_START[] = {0x45, 0x00, 0x00, 250 - 1, 0};// set ram_y_address_start_end
static const uint8_t RAM_X_POS[] = {0x4E, 0x01};              // set ram_x_address_counter
static const uint8_t RAM_Y_POS[] = {0x4F, 0x00, 0x00};        // set ram_x_address_counter
static const uint8_t BORDER_PART[] = {0x3C, 0x80};              // border waveform
static const uint8_t BORDER_FULL[] = {0x3C, 0x05};              // border waveform
static const uint8_t SLEEP[] = {0x10, 0x01};              // border waveform
#define SEND(x) this->cmd_data(x, sizeof(x))

/******************************************************************************
function :	Turn On Display
parameter:
******************************************************************************/
void WaveshareEPaper2P13InV3::EPD_2in13_V3_TurnOnDisplay(void) {
  this->command(0x22); // Display Update Control
  data(0xc7);
  this->command(0x20); // Activate Display Update Sequence
  wait_until_idle_();
}

/******************************************************************************
function :	Turn On Display
parameter:
******************************************************************************/
void WaveshareEPaper2P13InV3::EPD_2in13_V3_TurnOnDisplay_Partial(void) {
  this->command(0x22); // Display Update Control
  data(0x0f);  // fast:0x0c, quality:0x0f, 0xcf
  this->command(0x20); // Activate Display Update Sequence
  wait_until_idle_();
}

/******************************************************************************
function :	Send lut data and configuration
parameter:
    lut :   lut data
******************************************************************************/
void WaveshareEPaper2P13InV3::write_lut_(const uint8_t *lut) {
  this->cmd_data(lut, sizeof(PARTIAL_LUT));
  SEND(CMD1);
  SEND(GATEV);
  SEND(SRCV);
  SEND(VCOM);
}

/******************************************************************************
function :	Setting the display window
parameter:
  Xstart : X-axis starting position
  Ystart : Y-axis starting position
  Xend : End position of X-axis
  Yend : End position of Y-axis
******************************************************************************/
void WaveshareEPaper2P13InV3::EPD_2in13_V3_SetWindows(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend) {
  this->command(0x44); // SET_RAM_X_ADDRESS_START_END_POSITION
  data((Xstart >> 3) & 0xFF);
  data((Xend >> 3) & 0xFF);

  this->command(0x45); // SET_RAM_Y_ADDRESS_START_END_POSITION
  data(Ystart & 0xFF);
  data((Ystart >> 8) & 0xFF);
  data(Yend & 0xFF);
  data((Yend >> 8) & 0xFF);
}

/******************************************************************************
function :	Set Cursor
parameter:
	Xstart : X-axis starting position
	Ystart : Y-axis starting position
******************************************************************************/
void WaveshareEPaper2P13InV3::EPD_2in13_V3_SetCursor(uint16_t Xstart, uint16_t Ystart) {
  this->command(0x4E); // SET_RAM_X_ADDRESS_COUNTER
  data(Xstart & 0xFF);

  this->command(0x4F); // SET_RAM_Y_ADDRESS_COUNTER
  data(Ystart & 0xFF);
  data((Ystart >> 8) & 0xFF);
}

/******************************************************************************
function :	Initialize the e-Paper register
parameter:
******************************************************************************/
void WaveshareEPaper2P13InV3::EPD_2in13_V3_Init(void) {
  this->send_reset_();
  delay(100);

  wait_until_idle_();
  this->command(0x12);  //SWRESET
  wait_until_idle_();

  this->command(0x01); //Driver output control
  data(0xf9);
  data(0x00);
  data(0x00);

  this->command(0x11); //data entry mode
  data(0x03);

  this->set_window_();
  //EPD_2in13_V3_SetWindows(0, 0, 122 - 1, 250 - 1);
  //EPD_2in13_V3_SetCursor(0, 0);

  this->command(0x3C); //BorderWaveform
  data(0x05);

  this->command(0x21); //  Display update control
  data(0x00);
  data(0x80);

  this->command(0x18); //Read built-in temperature sensor
  data(0x80);

  wait_until_idle_();
  write_lut_(FULL_LUT);
}

/******************************************************************************
function :	Clear screen
parameter:
******************************************************************************/
void WaveshareEPaper2P13InV3::EPD_2in13_V3_Clear(void) {
  uint16_t Width, Height;
  Width = (122 % 8 == 0) ? (122 / 8) : (122 / 8 + 1);
  Height = 250;

  this->command(0x24);
  for (uint16_t j = 0; j < Height; j++) {
    for (uint16_t i = 0; i < Width; i++) {
      data(0XFF);
    }
  }

  EPD_2in13_V3_TurnOnDisplay();
}

/******************************************************************************
function :	Sends the image buffer in RAM to e-Paper and displays
parameter:
	image : Image data
******************************************************************************/
void WaveshareEPaper2P13InV3::EPD_2in13_V3_Display(uint8_t *Image) {
  uint16_t Width, Height;
  Width = (122 % 8 == 0) ? (122 / 8) : (122 / 8 + 1);
  Height = 250;

  this->command(0x24);
  for (uint16_t j = 0; j < Height; j++) {
    for (uint16_t i = 0; i < Width; i++) {
      data(Image[i + j * Width]);
    }
  }

  EPD_2in13_V3_TurnOnDisplay();
}


/******************************************************************************
function :	Refresh a base image
parameter:
	image : Image data
******************************************************************************/
void WaveshareEPaper2P13InV3::EPD_2in13_V3_Display_Base(uint8_t *Image) {
  uint16_t Width, Height;
  Width = (122 % 8 == 0) ? (122 / 8) : (122 / 8 + 1);
  Height = 250;

  this->command(0x24);   //Write Black and White image to RAM
  for (uint16_t j = 0; j < Height; j++) {
    for (uint16_t i = 0; i < Width; i++) {
      data(Image[i + j * Width]);
    }
  }
  this->command(0x26);   //Write Black and White image to RAM
  for (uint16_t j = 0; j < Height; j++) {
    for (uint16_t i = 0; i < Width; i++) {
      data(Image[i + j * Width]);
    }
  }
  EPD_2in13_V3_TurnOnDisplay();
}

/******************************************************************************
function :	Sends the image buffer in RAM to e-Paper and partial refresh
parameter:
	image : Image data
******************************************************************************/
void WaveshareEPaper2P13InV3::EPD_2in13_V3_Display_Partial(uint8_t *Image) {
  uint16_t Width, Height;
  Width = (122 % 8 == 0) ? (122 / 8) : (122 / 8 + 1);
  Height = 250;

  //Reset
  this->send_reset_();

  write_lut_(PARTIAL_LUT);

  SEND(CMD5);
  SEND(BORDER_PART);
  SEND(UPSEQ);
  this->activate_();

  this->set_window_();
  //EPD_2in13_V3_SetWindows(0, 0, 122 - 1, 250 - 1);
  //EPD_2in13_V3_SetCursor(0, 0);

  this->write_buffer_();
  EPD_2in13_V3_TurnOnDisplay_Partial();
}

void WaveshareEPaper2P13InV3::activate_() {
  this->command(ACTIVATE);  //Activate Display Update Sequence
  wait_until_idle_();
}


void WaveshareEPaper2P13InV3::write_buffer_() {
  this->wait_until_idle_();
  this->command(WRITE_BUFFER);
  this->start_data_();
  this->write_array(this->buffer_, this->get_buffer_length_());
  this->end_data_();
}

void WaveshareEPaper2P13InV3::send_reset_() {
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->digital_write(false);
    delay(1);
    this->reset_pin_->digital_write(true);
  }
}

void WaveshareEPaper2P13InV3::setup() {
  this->setup_pins_();
  delay(20);
  this->EPD_2in13_V3_Init();
  //this->EPD_2in13_V3_Clear();
  /*
  this->send_reset_();
  delay(120); //NOLINT
  if (!this->wait_until_idle_())
    this->mark_failed();
  this->command(0x12); // SW reset
  if (!this->wait_until_idle_())
    this->mark_failed();
  SEND(INIT1);
  SEND(INIT2);
  SEND(RAM_X_START);
  SEND(RAM_Y_START);
  SEND(RAM_X_POS);
  SEND(RAM_Y_POS);
  SEND(BORDER_FULL);
  SEND(INIT3);
  SEND(INIT4);
  this->wait_until_idle_();
  SEND(FULL_LUT);
  this->wait_until_idle_();
  SEND(CMD1);
  SEND(CMD2);
  SEND(CMD3);
  SEND(CMD4);
  this->wait_until_idle_();
  SEND(FULL_LUT);
  this->command(0x24);
  size_t bytes = this->get_width_internal() * this->get_height_internal() / 8;
  while (bytes-- != 0)
    this->data(0xFF);
  // write a white base image
  this->command(0x26);
  bytes = this->get_width_internal() * this->get_height_internal() / 8;
  while (bytes-- != 0)
    this->data(0xFF);
  SEND(ON_FULL);
  this->command(0x20);
   */
}

void WaveshareEPaper2P13InV3::set_window_() {
  SEND(RAM_X_START);
  SEND(RAM_Y_START);
  SEND(RAM_X_POS);
  SEND(RAM_Y_POS);
}

void WaveshareEPaper2P13InV3::initialize() {}

void WaveshareEPaper2P13InV3::partial_update_() {
  ESP_LOGD(TAG, "Performing partial e-paper update.");
  this->EPD_2in13_V3_Display_Partial(this->buffer_);
  /*
  SEND(CMD5);
  SEND(RAM_PP);
  SEND(BORDER_PART);
  SEND(UPSEQ);
  SEND(PARTIAL_LUT);
  this->command(0x20);
  this->set_timeout(100, [this] {
    this->write_buffer_();
    SEND(ON_PARTIAL);
    this->command(0x20);
    this->is_busy_ = false;
  });
   */
}

void WaveshareEPaper2P13InV3::full_update_() {
  ESP_LOGI(TAG, "Performing full e-paper update.");
  this->EPD_2in13_V3_Display(this->buffer_);
  /*
  SEND(FULL_LUT); // this may take some time
  this->set_timeout(750, [this] {
    this->write_buffer_();
    SEND(ON_FULL);
    this->command(0x20);
    this->is_busy_ = false;
  });
   */
}

void WaveshareEPaper2P13InV3::display() {
  /*
  if (this->is_busy_)
   return;
  this->is_busy_ = true;
  this->wait_until_idle_();
  this->set_window_();

   */
  const bool partial = this->at_update_ != 0;
  this->at_update_ = (this->at_update_ + 1) % this->full_update_every_;
  if (partial) {
    this->partial_update_();
  } else {
    this->full_update_();
  }
  ESP_LOGD(TAG, "Completed e-paper update.");
}

int WaveshareEPaper2P13InV3::get_width_internal() { return 128; }

int WaveshareEPaper2P13InV3::get_height_internal() { return 250; }

uint32_t WaveshareEPaper2P13InV3::idle_timeout_() { return 5000; }

void WaveshareEPaper2P13InV3::dump_config() {
  LOG_DISPLAY("", "Waveshare E-Paper", this);
  ESP_LOGCONFIG(TAG, "  Model: 2.13inV3");
  LOG_PIN("  CS Pin: ", this->cs_);
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  LOG_PIN("  DC Pin: ", this->dc_pin_);
  LOG_PIN("  Busy Pin: ", this->busy_pin_);
  LOG_UPDATE_INTERVAL(this);
}

void WaveshareEPaper2P13InV3::set_full_update_every(uint32_t full_update_every) {
  this->full_update_every_ = full_update_every;
}

}  // namespace waveshare_epaper
}  // namespace esphome

/*
  ili9341 display library
  copied-pasted huge amount of code from esp-idf examples
*/

#include "ili9341.h"

//Send a command to the LCD. Uses spi_device_transmit, which waits until the transfer is complete.
void lcd_cmd(spi_device_handle_t spi, const uint8_t cmd) {
  esp_err_t ret;
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));       //Zero out the transaction
  t.length=8;                     //Command is 8 bits
  t.tx_buffer=&cmd;               //The data is the cmd itself
  t.user=(void*)0;                //D/C needs to be set to 0
  ret=spi_device_transmit(spi, &t);  //Transmit!
  assert(ret==ESP_OK);            //Should have had no issues.
}

//Send data to the LCD. Uses spi_device_transmit, which waits until the transfer is complete.
void lcd_data(spi_device_handle_t spi, const uint8_t *data, int len) {
  esp_err_t ret;
  spi_transaction_t t;
  if (len==0) return;             //no need to send anything
  memset(&t, 0, sizeof(t));       //Zero out the transaction
  t.length=len*8;                 //Len is in bytes, transaction length is in bits.
  t.tx_buffer=data;               //Data
  t.user=(void*)1;                //D/C needs to be set to 1
  ret=spi_device_transmit(spi, &t);  //Transmit!
  assert(ret==ESP_OK);            //Should have had no issues.
}

//This function is called (in irq context!) just before a transmission starts. It will
//set the D/C line to the value indicated in the user field.
void lcd_spi_pre_transfer_callback(spi_transaction_t *t) {
  int dc=(int)t->user;
  gpio_set_level(PIN_DC, dc);
}


void lcd_init(spi_device_handle_t spi) {
  int cmd = 0;

  //Initialize non-SPI GPIOs
  gpio_set_direction(PIN_DC, GPIO_MODE_OUTPUT);
  gpio_set_direction(PIN_RST, GPIO_MODE_OUTPUT);
//  gpio_set_direction(PIN_BCKL, GPIO_MODE_OUTPUT);

  //reset the display
  gpio_set_level(PIN_RST, 0);
  vTaskDelay(100 / portTICK_RATE_MS);
  gpio_set_level(PIN_RST, 1);
  vTaskDelay(100 / portTICK_RATE_MS);

  //Send all the commands
  while (lcd_init_cmds[cmd].databytes!=0xff) {
      lcd_cmd(spi, lcd_init_cmds[cmd].cmd);
      lcd_data(spi, lcd_init_cmds[cmd].data, lcd_init_cmds[cmd].databytes&0x1F);
      if (lcd_init_cmds[cmd].databytes&0x80) {
          vTaskDelay(100 / portTICK_RATE_MS);
      }
      cmd++;
  }

  //Enable backlight
  //gpio_set_level(PIN_BCKL, 0);
}

//To send a set of lines we have to send a command, 2 data bytes, another command, 2 more data bytes and another command
//before sending the line data itself; a total of 6 transactions. (We can't put all of this in just one transaction
//because the D/C line needs to be toggled in the middle.)
//This routine queues these commands up so they get sent as quickly as possible.
void send_lines(spi_device_handle_t spi, int ypos, uint16_t *linedata) {
  assert(ypos >= 0 && ypos <= LCD_HEIGHT);
  esp_err_t ret;
  int x;
  //Transaction descriptors. Declared static so they're not allocated on the stack; we need this memory even when this
  //function is finished because the SPI driver needs access to it even while we're already calculating the next line.
  static spi_transaction_t trans[6];

  //In theory, it's better to initialize trans and data only once and hang on to the initialized
  //variables. We allocate them on the stack, so we need to re-init them each call.
  for (x=0; x<6; x++) {
      memset(&trans[x], 0, sizeof(spi_transaction_t));
      if ((x&1)==0) {
          //Even transfers are commands
          trans[x].length=8;
          trans[x].user=(void*)0;
      } else {
          //Odd transfers are data
          trans[x].length=8*4;
          trans[x].user=(void*)1;
      }
      trans[x].flags=SPI_TRANS_USE_TXDATA;
  }
  trans[0].tx_data[0]=0x2A;           //Column Address Set
  trans[1].tx_data[0]=0;              //Start Col High
  trans[1].tx_data[1]=0;              //Start Col Low
  trans[1].tx_data[2]=(320)>>8;       //End Col High
  trans[1].tx_data[3]=(320)&0xff;     //End Col Low
  trans[2].tx_data[0]=0x2B;           //Page address set
  trans[3].tx_data[0]=ypos>>8;        //Start page high
  trans[3].tx_data[1]=ypos&0xff;      //start page low
  trans[3].tx_data[2]=(ypos+PARALLEL_LINES)>>8;    //end page high
  trans[3].tx_data[3]=(ypos+PARALLEL_LINES)&0xff;  //end page low
  trans[4].tx_data[0]=0x2C;           //memory write
  trans[5].tx_buffer=linedata;        //finally send the line data
  trans[5].length=320*2*8*PARALLEL_LINES;          //Data length, in bits
  trans[5].flags=0; //undo SPI_TRANS_USE_TXDATA flag

  //Queue all transactions.
  for (x=0; x<6; x++) {
      ret=spi_device_queue_trans(spi, &trans[x], portMAX_DELAY);
      assert(ret==ESP_OK);
  }

  //When we are here, the SPI driver is busy (in the background) getting the transactions sent. That happens
  //mostly using DMA, so the CPU doesn't have much to do here. We're not going to wait for the transaction to
  //finish because we may as well spend the time calculating the next line. When that is done, we can call
  //send_line_finish, which will wait for the transfers to be done and check their status.
}

void send_line_finish(spi_device_handle_t spi) {
  spi_transaction_t *rtrans;
  esp_err_t ret;
  //Wait for all 6 transactions to be done and get back the results.
  for (int x=0; x<6; x++) {
      ret=spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
      assert(ret==ESP_OK);
      //We could inspect rtrans now if we received any info back. The LCD is treated as write-only, though.
  }
}

uint16_t color_to_uint(color_t color) {
  uint16_t ret = ((color.r & 0xF8) << 8) | ((color.g & 0xFC) << 3) | ((color.b & 0xF8) >> 3);
  return (ret>>8) | (ret << 8);
}

uint16_t bgr_to_uint(uint8_t b, uint8_t g, uint8_t r) {
  uint16_t ret = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);
  return (ret>>8) | (ret << 8);
}


void invert_display(spi_device_handle_t spi, bool inv) {
  lcd_cmd(spi, inv ? ILI9341_INVON : ILI9341_INVOFF);
}

void send_area(spi_device_handle_t spi, int x0, int x1, int y0, int y1, uint16_t *data) {
  esp_err_t ret;
  int x;
  //Transaction descriptors. Declared static so they're not allocated on the stack; we need this memory even when this
  //function is finished because the SPI driver needs access to it even while we're already calculating the next line.
  static spi_transaction_t trans[6];

  //In theory, it's better to initialize trans and data only once and hang on to the initialized
  //variables. We allocate them on the stack, so we need to re-init them each call.
  for (x=0; x<6; x++) {
      memset(&trans[x], 0, sizeof(spi_transaction_t));
      if ((x&1)==0) {
          //Even transfers are commands
          trans[x].length=8;
          trans[x].user=(void*)0;
      } else {
          //Odd transfers are data
          trans[x].length=8*4;
          trans[x].user=(void*)1;
      }
      trans[x].flags=SPI_TRANS_USE_TXDATA;
  }
  trans[0].tx_data[0]=0x2A;           //Column Address Set
  trans[1].tx_data[0]=x0 >> 8;              //Start Col High
  trans[1].tx_data[1]=x0 && 0xff;              //Start Col Low
  trans[1].tx_data[2]=(x1 ) >> 8;       //End Col High
  trans[1].tx_data[3]=(x1 ) & 0xff;     //End Col Low
  trans[2].tx_data[0]=0x2B;           //Page address set
  trans[3].tx_data[0]=y0 >> 8;        //Start page high
  trans[3].tx_data[1]=y0 & 0xff;      //start page low
  trans[3].tx_data[2]=(y1 ) >> 8;    //end page high
  trans[3].tx_data[3]=(y1 ) & 0xff;  //end page low
  trans[4].tx_data[0]=0x2C;           //memory write
  trans[5].tx_buffer=data;        //finally send the line data
  trans[5].length=(x1 - x0 + 1)*2*8*(y1 - y0 + 1);          //Data length, in bits
  trans[5].flags=0; //undo SPI_TRANS_USE_TXDATA flag

  //Queue all transactions.
  for (x=0; x<6; x++) {
    ret=spi_device_queue_trans(spi, &trans[x], portMAX_DELAY);
    assert(ret==ESP_OK);
  }
}
void send_area_finish(spi_device_handle_t spi) {
  spi_transaction_t *rtrans;
  esp_err_t ret;
  //Wait for all 6 transactions to be done and get back the results.
  for (int x=0; x<6; x++) {
      ret=spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
      assert(ret==ESP_OK);
      //We could inspect rtrans now if we received any info back. The LCD is treated as write-only, though.
  }
}

void lcd_fill(spi_device_handle_t spi, uint16_t color) {
  uint16_t data[320];
  for (int i = 0; i < 320; ++i) data[i] = color;
  for (int y = 0; y <= 241; ++y) {
    if(y != 0) send_area_finish(spi);
    send_area(spi, 0, 320, y, y, data);
  }
}

void drawChar(spi_device_handle_t spi, int x, int y, char c, uint16_t fcolor, uint16_t bcolor) {

}

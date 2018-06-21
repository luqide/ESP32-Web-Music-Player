/*
  ili9341 display library
  copied-pasted huge amount of code from esp-idf examples
*/

#include "ili9341.h"
//IRAM_ATTR uint16_t vBuffer[LCD_HEIGHT * LCD_WIDTH];

void _swap_int16_t(int a, int b) {
  int tmp = a;
  a = b;
  b = tmp;
}

//Send a command to the LCD. Uses spi_device_transmit, which waits until the transfer is complete.
void lcd_cmd(const uint8_t cmd) {
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
void lcd_data(const uint8_t *data, int len) {
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


void lcd_init() {
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
      lcd_cmd(lcd_init_cmds[cmd].cmd);
      lcd_data(lcd_init_cmds[cmd].data, lcd_init_cmds[cmd].databytes&0x1F);
      if (lcd_init_cmds[cmd].databytes&0x80) {
          vTaskDelay(100 / portTICK_RATE_MS);
      }
      cmd++;
  }

  //Enable backlight
  //gpio_set_level(PIN_BCKL, 0);
}


uint16_t color_to_uint(color_t color) {
  uint16_t ret = ((color.r & 0xF8) << 8) | ((color.g & 0xFC) << 3) | ((color.b & 0xF8) >> 3);
  return (ret>>8) | (ret << 8);
}

uint16_t bgr_to_uint(uint8_t b, uint8_t g, uint8_t r) {
  uint16_t ret = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);
  return (ret>>8) | (ret << 8);
}

static void send_lines(int ypos, uint16_t *linedata) {
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


static void send_line_finish() {
  spi_transaction_t *rtrans;
  esp_err_t ret;
  //Wait for all 6 transactions to be done and get back the results.
  for (int x=0; x<6; x++) {
      ret=spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
      assert(ret==ESP_OK);
      //We could inspect rtrans now if we received any info back. The LCD is treated as write-only, though.
  }
}

void invert_display(bool inv) {
  lcd_cmd(inv ? ILI9341_INVON : ILI9341_INVOFF);
}

void drawPixel(int x0, int y0, uint16_t color) {
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
  trans[0].tx_data[0] = 0x2A;           //Column Address Set
  trans[1].tx_data[0] = x0 >> 8;              //Start Col High
  trans[1].tx_data[1] = x0;              //Start Col Low
  trans[1].tx_data[2] = x0 >> 8;       //End Col High
  trans[1].tx_data[3] = x0;     //End Col Low
  trans[2].tx_data[0] = 0x2B;           //Page address set
  trans[3].tx_data[0] = y0 >> 8;        //Start page high
  trans[3].tx_data[1] = y0;      //start page low
  trans[3].tx_data[2] = y0 >> 8;    //end page high
  trans[3].tx_data[3] = y0;  //end page low
  trans[4].tx_data[0] = 0x2C;           //memory write
  trans[5].tx_data[0] = color >> 8;
  trans[5].tx_data[1] = color;
  trans[5].length = 8 * 2;

  spi_transaction_t *rtrans;
  for (x=0; x<6; x++) {
      ret=spi_device_queue_trans(spi, &trans[x], portMAX_DELAY);
      assert(ret==ESP_OK);
      ret=spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
      assert(ret==ESP_OK);
  }

}

void lcd_fill(uint16_t color) {
  uint16_t *data = heap_caps_malloc(320*PARALLEL_LINES*sizeof(uint16_t), MALLOC_CAP_DMA);
  assert(data != NULL);
  memset(data, color, 320*PARALLEL_LINES*sizeof(uint16_t));
  for (int y = 0; y < 240; y += PARALLEL_LINES) {
    if(y != 0) send_line_finish();
    send_lines(y, data);
  }
}

void drawLine(int x0, int x1, int y0, int y1, uint16_t color) {
    int steep = abs(y1 - y0) > abs(x1 - x0);
    if (steep) {
        _swap_int16_t(x0, y0);
        _swap_int16_t(x1, y1);
    }

    if (x0 > x1) {
        _swap_int16_t(x0, x1);
        _swap_int16_t(y0, y1);
    }

    int dx, dy;
    dx = x1 - x0;
    dy = abs(y1 - y0);

    int err = dx / 2;
    int ystep;

    if (y0 < y1) {
        ystep = 1;
    } else {
        ystep = -1;
    }

    for (; x0<=x1; x0++) {
        if (steep) {
            drawPixel(y0, x0, color);
        } else {
            drawPixel(x0, y0, color);
        }
        err -= dy;
        if (err < 0) {
            y0 += ystep;
            err += dx;
        }
    }
}

void drawFastVLine(int x, int y, int h, uint16_t color) {
  drawLine(x, x, y, y+h-1, color);
}

void drawFastHLine(int x, int y, int w, uint16_t color) {
  drawLine(x, x+w-1, y, y, color);
}

void fillRect(int x, int y, int w, int h, uint16_t color) {
    for (int i=x; i<x+w; i++) {
        drawFastVLine(i, y, h, color);
    }
}

// Draw a circle outline
void drawCircle(int x0, int y0, int r, uint16_t color) {
    int f = 1 - r;
    int ddF_x = 1;
    int ddF_y = -2 * r;
    int x = 0;
    int y = r;

    drawPixel(x0  , y0+r, color);
    drawPixel(x0  , y0-r, color);
    drawPixel(x0+r, y0  , color);
    drawPixel(x0-r, y0  , color);

    while (x<y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        drawPixel(x0 + x, y0 + y, color);
        drawPixel(x0 - x, y0 + y, color);
        drawPixel(x0 + x, y0 - y, color);
        drawPixel(x0 - x, y0 - y, color);
        drawPixel(x0 + y, y0 + x, color);
        drawPixel(x0 - y, y0 + x, color);
        drawPixel(x0 + y, y0 - x, color);
        drawPixel(x0 - y, y0 - x, color);
    }
}

void drawCircleHelper( int x0, int y0, int r, uint8_t cornername, uint16_t color) {
    int f     = 1 - r;
    int ddF_x = 1;
    int ddF_y = -2 * r;
    int x     = 0;
    int y     = r;

    while (x<y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f     += ddF_y;
        }
        x++;
        ddF_x += 2;
        f     += ddF_x;
        if (cornername & 0x4) {
            drawPixel(x0 + x, y0 + y, color);
            drawPixel(x0 + y, y0 + x, color);
        }
        if (cornername & 0x2) {
            drawPixel(x0 + x, y0 - y, color);
            drawPixel(x0 + y, y0 - x, color);
        }
        if (cornername & 0x8) {
            drawPixel(x0 - y, y0 + x, color);
            drawPixel(x0 - x, y0 + y, color);
        }
        if (cornername & 0x1) {
            drawPixel(x0 - y, y0 - x, color);
            drawPixel(x0 - x, y0 - y, color);
        }
    }
}

// Used to do circles and roundrects
void fillCircleHelper(int x0, int y0, int r, uint8_t cornername, int delta, uint16_t color) {

    int f     = 1 - r;
    int ddF_x = 1;
    int ddF_y = -2 * r;
    int x     = 0;
    int y     = r;

    while (x<y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f     += ddF_y;
        }
        x++;
        ddF_x += 2;
        f     += ddF_x;

        if (cornername & 0x1) {
            drawFastVLine(x0+x, y0-y, 2*y+1+delta, color);
            drawFastVLine(x0+y, y0-x, 2*x+1+delta, color);
        }
        if (cornername & 0x2) {
            drawFastVLine(x0-x, y0-y, 2*y+1+delta, color);
            drawFastVLine(x0-y, y0-x, 2*x+1+delta, color);
        }
    }
}

void fillCircle(int x0, int y0, int r, uint16_t color) {
    drawFastVLine(x0, y0-r, 2*r+1, color);
    fillCircleHelper(x0, y0, r, 3, 0, color);
}

// Draw a rectangle
void drawRect(int x, int y, int w, int h, uint16_t color) {
    drawFastHLine(x, y, w, color);
    drawFastHLine(x, y+h-1, w, color);
    drawFastVLine(x, y, h, color);
    drawFastVLine(x+w-1, y, h, color);
}

// Draw a rounded rectangle
void drawRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
    // smarter version
    drawFastHLine(x+r  , y    , w-2*r, color); // Top
    drawFastHLine(x+r  , y+h-1, w-2*r, color); // Bottom
    drawFastVLine(x    , y+r  , h-2*r, color); // Left
    drawFastVLine(x+w-1, y+r  , h-2*r, color); // Right
    // draw four corners
    drawCircleHelper(x+r    , y+r    , r, 1, color);
    drawCircleHelper(x+w-r-1, y+r    , r, 2, color);
    drawCircleHelper(x+w-r-1, y+h-r-1, r, 4, color);
    drawCircleHelper(x+r    , y+h-r-1, r, 8, color);
}

// Fill a rounded rectangle
void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
    // smarter version
    fillRect(x+r, y, w-2*r, h, color);

    // draw four corners
    fillCircleHelper(x+w-r-1, y+r, r, 1, h-2*r-1, color);
    fillCircleHelper(x+r    , y+r, r, 2, h-2*r-1, color);
}

// Draw a triangle
void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color) {
    drawLine(x0, x1, y0, y1, color);
    drawLine(x1, x2, y1, y2, color);
    drawLine(x2, x2, y2, y0, color);
}

// Fill a triangle
void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color) {

    int a, b, y, last;

    // Sort coordinates by Y order (y2 >= y1 >= y0)
    if (y0 > y1) {
        _swap_int16_t(y0, y1); _swap_int16_t(x0, x1);
    }
    if (y1 > y2) {
        _swap_int16_t(y2, y1); _swap_int16_t(x2, x1);
    }
    if (y0 > y1) {
        _swap_int16_t(y0, y1); _swap_int16_t(x0, x1);
    }

    if(y0 == y2) { // Handle awkward all-on-same-line case as its own thing
        a = b = x0;
        if(x1 < a)      a = x1;
        else if(x1 > b) b = x1;
        if(x2 < a)      a = x2;
        else if(x2 > b) b = x2;
        drawFastHLine(a, y0, b-a+1, color);
        return;
    }

    int
    dx01 = x1 - x0,
    dy01 = y1 - y0,
    dx02 = x2 - x0,
    dy02 = y2 - y0,
    dx12 = x2 - x1,
    dy12 = y2 - y1;
    int32_t
    sa   = 0,
    sb   = 0;

    // For upper part of triangle, find scanline crossings for segments
    // 0-1 and 0-2.  If y1=y2 (flat-bottomed triangle), the scanline y1
    // is included here (and second loop will be skipped, avoiding a /0
    // error there), otherwise scanline y1 is skipped here and handled
    // in the second loop...which also avoids a /0 error here if y0=y1
    // (flat-topped triangle).
    if(y1 == y2) last = y1;   // Include y1 scanline
    else         last = y1-1; // Skip it

    for(y=y0; y<=last; y++) {
        a   = x0 + sa / dy01;
        b   = x0 + sb / dy02;
        sa += dx01;
        sb += dx02;
        /* longhand:
        a = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        b = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        */
        if(a > b) _swap_int16_t(a,b);
        drawFastHLine(a, y, b-a+1, color);
    }

    // For lower part of triangle, find scanline crossings for segments
    // 0-2 and 1-2.  This loop is skipped if y1=y2.
    sa = dx12 * (y - y1);
    sb = dx02 * (y - y0);
    for(; y<=y2; y++) {
        a   = x1 + sa / dy12;
        b   = x0 + sb / dy02;
        sa += dx12;
        sb += dx02;
        /* longhand:
        a = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
        b = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        */
        if(a > b) _swap_int16_t(a,b);
        drawFastHLine(a, y, b-a+1, color);
    }
}

// BITMAP / XBITMAP / GRAYSCALE / RGB BITMAP FUNCTIONS ---------------------

// Draw a PROGMEM-resident 1-bit image at the specified (x,y) position,
// using the specified foreground color (unset bits are transparent).
void drawBitmap_1bit(int x, int y, const uint8_t bitmap[], int w, int h, uint16_t color) {

    int byteWidth = (w + 7) / 8; // Bitmap scanline pad = whole byte
    uint8_t byte = 0;

    for(int j=0; j<h; j++, y++) {
        for(int i=0; i<w; i++) {
            if(i & 7) byte <<= 1;
            else      byte   = bitmap[j * byteWidth + i / 8];
            if(byte & 0x80) drawPixel(x+i, y, color);
        }
      }
}

void drawRGBBitmap(int x, int y, uint16_t *bitmap, int w, int h) {
  for(int j=0; j<h; j++, y++) {
      for(int i=0; i<w; i++ ) {
          drawPixel(x+i, y, bitmap[j * w + i]);
      }
  }
}

void drawChar(int x, int y, unsigned char c, uint16_t color, uint16_t bg, uint8_t size) {
  c -= gfxFont->first;
  GFXglyph *glyph  = &((gfxFont->glyph)[c]);
  uint8_t  *bitmap = gfxFont->bitmap;

  uint16_t bo = glyph->bitmapOffset;
  uint8_t  w  = glyph->width,
           h  = glyph->height;  int8_t   xo = glyph->xOffset,
           yo = glyph->yOffset;
  uint8_t  xx, yy, bits = 0, bit = 0;
  int16_t  xo16 = 0, yo16 = 0;

  if(size > 1) {
      xo16 = xo;
      yo16 = yo;
  }

  for(yy=0; yy<h; yy++) {
      for(xx=0; xx<w; xx++) {
          if(!(bit++ & 7)) {
              bits = bitmap[bo++];
          }
          if(bits & 0x80) {
              if(size == 1) {
                  drawPixel(x+xo+xx, y+yo+yy, color);
              } else {
                  fillRect(y+(yo16+yy)*size, x+(xo16+xx)*size,
                    size, size, color);
              }
          }
          bits <<= 1;
      }
  }
}

void setCursor(int x, int y) {
  if(x < 0 || y < 0 || x >= LCD_WIDTH || y >= LCD_HEIGHT)
    return;
  cursor_x = x;
  cursor_y = y;
}

void setTextsize(int size) {
  textsize = size > 0 ? size : 1;
}

void setTextcolor(uint16_t c) {
  textcolor = c;
}

void setTextBgcolor(uint16_t c) {
  textbgcolor = c;
}

void setTextwrap(bool w) {
  wrap = w;
}

void writeChar(char c) {
  if(c == '\n') {
      cursor_x  = 0;
      cursor_y += textsize * (gfxFont->yAdvance);
  } else if(c != '\r') {
      uint8_t first = gfxFont->first;
      if((c >= first) && (c <= gfxFont->last)) {
          GFXglyph *glyph = &((gfxFont->glyph)[c - first]);
          uint8_t   w     = glyph->width,
                    h     = glyph->height;
          if((w > 0) && (h > 0)) {
              // if((cursor_x + (int16_t)textsize * (glyph->xAdvance + w)) >= LCD_WIDTH) {
              //   writeChar('\n');
              // }
              drawChar(cursor_x, cursor_y, c, textcolor, textbgcolor, textsize);
          }
          cursor_x += glyph->xAdvance * (int16_t)textsize;
      }
  }
}

void writeString(char *str) {
  for(int i = 0; i < strlen(str); ++i) {
    writeChar(str[i]);
  }
}

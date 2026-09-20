/**
 * ili9488.c
 *
 * See ili9488.h for API documentation and wiring notes.
 */

#include "ili9488.h"
#include <stdlib.h> /* abs() */

/* ------------------------------------------------------------------------ */
/* ILI9488 command set (subset used by this driver)                         */
/* ------------------------------------------------------------------------ */
#define ILI9488_CMD_SWRESET        0x01
#define ILI9488_CMD_SLPIN          0x10
#define ILI9488_CMD_SLPOUT         0x11
#define ILI9488_CMD_INVOFF         0x20
#define ILI9488_CMD_INVON          0x21
#define ILI9488_CMD_DISPOFF        0x28
#define ILI9488_CMD_DISPON         0x29
#define ILI9488_CMD_CASET          0x2A
#define ILI9488_CMD_PASET          0x2B
#define ILI9488_CMD_RAMWR          0x2C
#define ILI9488_CMD_MADCTL         0x36
#define ILI9488_CMD_COLMOD         0x3A
#define ILI9488_CMD_FRMCTR1        0xB1
#define ILI9488_CMD_INVCTR         0xB4
#define ILI9488_CMD_DFUNCTR        0xB6
#define ILI9488_CMD_PWCTR1         0xC0
#define ILI9488_CMD_PWCTR2         0xC1
#define ILI9488_CMD_VMCTR1         0xC5
#define ILI9488_CMD_PGAMCTRL       0xE0
#define ILI9488_CMD_NGAMCTRL       0xE1
#define ILI9488_CMD_ADJCTRL3       0xF7

/* MADCTL bits */
#define MADCTL_MY   0x80
#define MADCTL_MX   0x40
#define MADCTL_MV   0x20
#define MADCTL_ML   0x10
#define MADCTL_BGR  0x08
#define MADCTL_MH   0x04

/* SPI transaction timeout, ms */
#define ILI9488_SPI_TIMEOUT   200

/* ------------------------------------------------------------------------ */
/* Low-level pin/SPI helpers                                                */
/* ------------------------------------------------------------------------ */

static void CS_Select(ILI9488_HandleTypeDef *dev)
{
    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);     /* Deselect SD Card */

    if (dev->cs_port) {
        HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET);
    }
}

static void CS_Deselect(ILI9488_HandleTypeDef *dev)
{
    if (dev->cs_port) {
        HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);
    }
}

static void DC_Command(ILI9488_HandleTypeDef *dev)
{
    HAL_GPIO_WritePin(dev->dc_port, dev->dc_pin, GPIO_PIN_RESET);
}

static void DC_Data(ILI9488_HandleTypeDef *dev)
{
    HAL_GPIO_WritePin(dev->dc_port, dev->dc_pin, GPIO_PIN_SET);
}

static void SPI_Write(ILI9488_HandleTypeDef *dev, const uint8_t *data, size_t len)
{
    /* HAL_SPI_Transmit takes a non-const pointer; cast is safe, we never write. */
    HAL_SPI_Transmit(dev->hspi, (uint8_t *)data, (uint16_t)len, ILI9488_SPI_TIMEOUT);
}

static void WriteCommand(ILI9488_HandleTypeDef *dev, uint8_t cmd)
{
    DC_Command(dev);
    SPI_Write(dev, &cmd, 1);
}

static void WriteData(ILI9488_HandleTypeDef *dev, const uint8_t *data, size_t len)
{
    DC_Data(dev);
    SPI_Write(dev, data, len);
}

static void WriteDataByte(ILI9488_HandleTypeDef *dev, uint8_t data)
{
    WriteData(dev, &data, 1);
}

/* Convert a 16-bit RGB565 color to 3 bytes of 18-bit RGB666 for the panel. */
static inline void Color565To666(uint16_t color565, uint8_t out[3])
{
    uint8_t r5 = (color565 >> 11) & 0x1F;
    uint8_t g6 = (color565 >> 5)  & 0x3F;
    uint8_t b5 =  color565        & 0x1F;

    /* Expand to 6 bits each, left-justified in the byte (panel reads top 6 bits). */
    out[0] = (uint8_t)((r5 << 3) | (r5 >> 2));   /* R: 5->8 bits, ILI9488 uses upper 6 */
    out[1] = (uint8_t)((g6 << 2) | (g6 >> 4));   /* G: 6->8 bits */
    out[2] = (uint8_t)((b5 << 3) | (b5 >> 2));   /* B: 5->8 bits */
}

/* ------------------------------------------------------------------------ */
/* Reset / power                                                            */
/* ------------------------------------------------------------------------ */

void ILI9488_HardwareReset(ILI9488_HandleTypeDef *dev)
{
    if (!dev->rst_port) {
        return;
    }
    HAL_GPIO_WritePin(dev->rst_port, dev->rst_pin, GPIO_PIN_SET);
    HAL_Delay(5);
    HAL_GPIO_WritePin(dev->rst_port, dev->rst_pin, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(dev->rst_port, dev->rst_pin, GPIO_PIN_SET);
    HAL_Delay(150);
}

void ILI9488_SoftwareReset(ILI9488_HandleTypeDef *dev)
{
    CS_Select(dev);
    WriteCommand(dev, ILI9488_CMD_SWRESET);
    CS_Deselect(dev);
    HAL_Delay(150);
}

void ILI9488_Sleep(ILI9488_HandleTypeDef *dev, bool on)
{
    CS_Select(dev);
    WriteCommand(dev, on ? ILI9488_CMD_SLPIN : ILI9488_CMD_SLPOUT);
    CS_Deselect(dev);
    HAL_Delay(on ? 5 : 120);
}

void ILI9488_DisplayOn(ILI9488_HandleTypeDef *dev, bool on)
{
    CS_Select(dev);
    WriteCommand(dev, on ? ILI9488_CMD_DISPON : ILI9488_CMD_DISPOFF);
    CS_Deselect(dev);
}

void ILI9488_BacklightOn(ILI9488_HandleTypeDef *dev, bool on)
{
    if (!dev->bl_port) {
        return;
    }
    HAL_GPIO_WritePin(dev->bl_port, dev->bl_pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void ILI9488_InvertColors(ILI9488_HandleTypeDef *dev, bool invert)
{
    CS_Select(dev);
    WriteCommand(dev, invert ? ILI9488_CMD_INVON : ILI9488_CMD_INVOFF);
    CS_Deselect(dev);
}

/* ------------------------------------------------------------------------ */
/* Init sequence                                                            */
/* ------------------------------------------------------------------------ */

void ILI9488_Init(ILI9488_HandleTypeDef *dev, ILI9488_Rotation_t rotation)
{
    /* Ensure CS idle-high before anything else. */
    CS_Deselect(dev);

    ILI9488_HardwareReset(dev);
    ILI9488_SoftwareReset(dev);

    CS_Select(dev);

    /* Positive gamma control */
    {
        static const uint8_t gamma_p[] = {
            0x00, 0x03, 0x09, 0x08, 0x16, 0x0A, 0x3F, 0x78,
            0x4C, 0x09, 0x0A, 0x08, 0x16, 0x1A, 0x0F
        };
        WriteCommand(dev, ILI9488_CMD_PGAMCTRL);
        WriteData(dev, gamma_p, sizeof(gamma_p));
    }

    /* Negative gamma control */
    {
        static const uint8_t gamma_n[] = {
            0x00, 0x16, 0x19, 0x03, 0x0F, 0x05, 0x32, 0x45,
            0x46, 0x04, 0x0E, 0x0D, 0x35, 0x37, 0x0F
        };
        WriteCommand(dev, ILI9488_CMD_NGAMCTRL);
        WriteData(dev, gamma_n, sizeof(gamma_n));
    }

    /* Power control 1: Vreg1out, Verg2out */
    WriteCommand(dev, ILI9488_CMD_PWCTR1);
    WriteDataByte(dev, 0x17);
    WriteDataByte(dev, 0x15);

    /* Power control 2: VGH, VGL */
    WriteCommand(dev, ILI9488_CMD_PWCTR2);
    WriteDataByte(dev, 0x41);

    /* VCOM control */
    WriteCommand(dev, ILI9488_CMD_VMCTR1);
    WriteDataByte(dev, 0x00);
    WriteDataByte(dev, 0x12);
    WriteDataByte(dev, 0x80);

    /* Memory access control: RGB order + orientation set later via SetRotation */
    WriteCommand(dev, ILI9488_CMD_MADCTL);
    WriteDataByte(dev, 0x48); /* MX + BGR as a sane default; overwritten below */

    /* Interface pixel format: 18 bits/pixel (RGB666) - see driver header notes */
    WriteCommand(dev, ILI9488_CMD_COLMOD);
    WriteDataByte(dev, 0x66);

    /* Interface mode control */
    WriteCommand(dev, ILI9488_CMD_INVCTR);
    WriteDataByte(dev, 0x02);

    /* Frame rate control: 70 Hz */
    WriteCommand(dev, ILI9488_CMD_FRMCTR1);
    WriteDataByte(dev, 0xA0);

    /* Display function control */
    WriteCommand(dev, ILI9488_CMD_DFUNCTR);
    WriteDataByte(dev, 0x02);
    WriteDataByte(dev, 0x02);
    WriteDataByte(dev, 0x3B);

    /* Adjust control 3: keep DSI/SPI timing defaults recommended by datasheet */
    WriteCommand(dev, ILI9488_CMD_ADJCTRL3);
    WriteDataByte(dev, 0xA9);
    WriteDataByte(dev, 0x51);
    WriteDataByte(dev, 0x2C);
    WriteDataByte(dev, 0x82);

    WriteCommand(dev, ILI9488_CMD_SLPOUT);
    CS_Deselect(dev);
    HAL_Delay(120);

    CS_Select(dev);
    WriteCommand(dev, ILI9488_CMD_DISPON);
    CS_Deselect(dev);
    HAL_Delay(25);

    ILI9488_SetRotation(dev, rotation);
    ILI9488_BacklightOn(dev, true);
}

/* ------------------------------------------------------------------------ */
/* Rotation                                                                  */
/* ------------------------------------------------------------------------ */

void ILI9488_SetRotation(ILI9488_HandleTypeDef *dev, ILI9488_Rotation_t rotation)
{
    uint8_t madctl = 0;

    switch (rotation) {
        case ILI9488_ROTATION_0:
            madctl = MADCTL_MX | MADCTL_BGR;
            dev->width  = ILI9488_NATIVE_WIDTH;
            dev->height = ILI9488_NATIVE_HEIGHT;
            break;
        case ILI9488_ROTATION_90:
            madctl = MADCTL_MV | MADCTL_BGR;
            dev->width  = ILI9488_NATIVE_HEIGHT;
            dev->height = ILI9488_NATIVE_WIDTH;
            break;
        case ILI9488_ROTATION_180:
            madctl = MADCTL_MY | MADCTL_BGR;
            dev->width  = ILI9488_NATIVE_WIDTH;
            dev->height = ILI9488_NATIVE_HEIGHT;
            break;
        case ILI9488_ROTATION_270:
            madctl = MADCTL_MX | MADCTL_MY | MADCTL_MV | MADCTL_BGR;
            dev->width  = ILI9488_NATIVE_HEIGHT;
            dev->height = ILI9488_NATIVE_WIDTH;
            break;
    }

    dev->rotation = rotation;

    CS_Select(dev);
    WriteCommand(dev, ILI9488_CMD_MADCTL);
    WriteDataByte(dev, madctl);
    CS_Deselect(dev);
}

/* ------------------------------------------------------------------------ */
/* Address window / low-level pixel writes                                  */
/* ------------------------------------------------------------------------ */

void ILI9488_SetAddressWindow(ILI9488_HandleTypeDef *dev,
                               uint16_t x0, uint16_t y0,
                               uint16_t x1, uint16_t y1)
{
    uint8_t buf[4];

    WriteCommand(dev, ILI9488_CMD_CASET);
    buf[0] = (uint8_t)(x0 >> 8);
    buf[1] = (uint8_t)(x0 & 0xFF);
    buf[2] = (uint8_t)(x1 >> 8);
    buf[3] = (uint8_t)(x1 & 0xFF);
    WriteData(dev, buf, 4);

    WriteCommand(dev, ILI9488_CMD_PASET);
    buf[0] = (uint8_t)(y0 >> 8);
    buf[1] = (uint8_t)(y0 & 0xFF);
    buf[2] = (uint8_t)(y1 >> 8);
    buf[3] = (uint8_t)(y1 & 0xFF);
    WriteData(dev, buf, 4);

    WriteCommand(dev, ILI9488_CMD_RAMWR);
}

/* ------------------------------------------------------------------------ */
/* Drawing primitives                                                       */
/* ------------------------------------------------------------------------ */

void ILI9488_DrawPixel(ILI9488_HandleTypeDef *dev, uint16_t x, uint16_t y, uint16_t color565)
{
    if (x >= dev->width || y >= dev->height) {
        return;
    }

    uint8_t rgb[3];
    Color565To666(color565, rgb);

    CS_Select(dev);
    ILI9488_SetAddressWindow(dev, x, y, x, y);
    WriteData(dev, rgb, 3);
    CS_Deselect(dev);
}

void ILI9488_FillRect(ILI9488_HandleTypeDef *dev,
                       uint16_t x, uint16_t y,
                       uint16_t w, uint16_t h,
                       uint16_t color565)
{
    if (x >= dev->width || y >= dev->height) {
        return;
    }
    if ((uint32_t)x + w > dev->width)  { w = dev->width  - x; }
    if ((uint32_t)y + h > dev->height) { h = dev->height - y; }
    if (w == 0 || h == 0) {
        return;
    }

    uint8_t rgb[3];
    Color565To666(color565, rgb);

    /* Chunk the fill through a small stack buffer to limit RAM usage while
     * still avoiding a full HAL_SPI_Transmit() call per pixel. */
    uint8_t chunk[3 * 64];
    uint32_t total_pixels = (uint32_t)w * (uint32_t)h;
    uint32_t chunk_pixels = sizeof(chunk) / 3;

    for (uint32_t i = 0; i < chunk_pixels && i < total_pixels; i++) {
        chunk[3 * i]     = rgb[0];
        chunk[3 * i + 1] = rgb[1];
        chunk[3 * i + 2] = rgb[2];
    }

    CS_Select(dev);
    ILI9488_SetAddressWindow(dev, x, y, x + w - 1, y + h - 1);

    DC_Data(dev);
    while (total_pixels > 0) {
        uint32_t n = (total_pixels > chunk_pixels) ? chunk_pixels : total_pixels;
        SPI_Write(dev, chunk, n * 3);
        total_pixels -= n;
    }
    CS_Deselect(dev);
}

void ILI9488_FillScreen(ILI9488_HandleTypeDef *dev, uint16_t color565)
{
    ILI9488_FillRect(dev, 0, 0, dev->width, dev->height, color565);
}

void ILI9488_DrawFastHLine(ILI9488_HandleTypeDef *dev,
                            uint16_t x, uint16_t y, uint16_t w, uint16_t color565)
{
    ILI9488_FillRect(dev, x, y, w, 1, color565);
}

void ILI9488_DrawFastVLine(ILI9488_HandleTypeDef *dev,
                            uint16_t x, uint16_t y, uint16_t h, uint16_t color565)
{
    ILI9488_FillRect(dev, x, y, 1, h, color565);
}

void ILI9488_DrawRect(ILI9488_HandleTypeDef *dev,
                       uint16_t x, uint16_t y,
                       uint16_t w, uint16_t h,
                       uint16_t color565)
{
    if (w == 0 || h == 0) {
        return;
    }
    ILI9488_DrawFastHLine(dev, x, y, w, color565);
    ILI9488_DrawFastHLine(dev, x, y + h - 1, w, color565);
    ILI9488_DrawFastVLine(dev, x, y, h, color565);
    ILI9488_DrawFastVLine(dev, x + w - 1, y, h, color565);
}

void ILI9488_DrawLine(ILI9488_HandleTypeDef *dev,
                       int16_t x0, int16_t y0,
                       int16_t x1, int16_t y1,
                       uint16_t color565)
{
    /* Fast paths for axis-aligned lines */
    if (y0 == y1) {
        int16_t x_start = (x0 < x1) ? x0 : x1;
        uint16_t len = (uint16_t)((x0 < x1) ? (x1 - x0) : (x0 - x1)) + 1;
        ILI9488_DrawFastHLine(dev, (uint16_t)x_start, (uint16_t)y0, len, color565);
        return;
    }
    if (x0 == x1) {
        int16_t y_start = (y0 < y1) ? y0 : y1;
        uint16_t len = (uint16_t)((y0 < y1) ? (y1 - y0) : (y0 - y1)) + 1;
        ILI9488_DrawFastVLine(dev, (uint16_t)x0, (uint16_t)y_start, len, color565);
        return;
    }

    /* Bresenham's line algorithm */
    int16_t dx = (int16_t)abs(x1 - x0);
    int16_t dy = (int16_t)-abs(y1 - y0);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx + dy;

    while (1) {
        ILI9488_DrawPixel(dev, (uint16_t)x0, (uint16_t)y0, color565);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int16_t e2 = (int16_t)(2 * err);
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void ILI9488_DrawImage(ILI9488_HandleTypeDef *dev,
                        uint16_t x, uint16_t y,
                        uint16_t w, uint16_t h,
                        const uint16_t *data565)
{
    if (x >= dev->width || y >= dev->height || !data565) {
        return;
    }
    uint16_t draw_w = w;
    uint16_t draw_h = h;
    if ((uint32_t)x + draw_w > dev->width)  { draw_w = dev->width  - x; }
    if ((uint32_t)y + draw_h > dev->height) { draw_h = dev->height - y; }
    if (draw_w == 0 || draw_h == 0) {
        return;
    }

    /* Small fixed-size chunk buffer keeps stack usage low regardless of
     * image width; converted and streamed piecewise per row. */
    uint8_t chunk[3 * 64];
    const uint16_t chunk_pixels = sizeof(chunk) / 3;

    CS_Select(dev);
    ILI9488_SetAddressWindow(dev, x, y, x + draw_w - 1, y + draw_h - 1);
    DC_Data(dev);

    for (uint16_t row = 0; row < draw_h; row++) {
        const uint16_t *src_row = data565 + (uint32_t)row * w; /* source stride = original w */
        uint16_t col = 0;
        while (col < draw_w) {
            uint16_t n = (uint16_t)((draw_w - col) > chunk_pixels ? chunk_pixels : (draw_w - col));
            for (uint16_t i = 0; i < n; i++) {
                Color565To666(src_row[col + i], &chunk[3 * i]);
            }
            SPI_Write(dev, chunk, (size_t)n * 3);
            col += n;
        }
    }

    CS_Deselect(dev);
}

void ILI9488_DrawBitmap1Bit(ILI9488_HandleTypeDef *dev,
                             uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h,
                             const uint8_t *bitmap,
                             uint16_t fg565, uint16_t bg565)
{
    if (!bitmap) {
        return;
    }

    uint8_t fg[3], bg[3];
    Color565To666(fg565, fg);
    Color565To666(bg565, bg);

    uint16_t bytes_per_row = (w + 7) / 8;

    for (uint16_t row = 0; row < h; row++) {
        for (uint16_t col = 0; col < w; col++) {
            uint8_t byte = bitmap[row * bytes_per_row + (col / 8)];
            bool set = (byte >> (7 - (col % 8))) & 0x01;

            uint16_t px = x + col;
            uint16_t py = y + row;
            if (px >= dev->width || py >= dev->height) {
                continue;
            }

            CS_Select(dev);
            ILI9488_SetAddressWindow(dev, px, py, px, py);
            WriteData(dev, set ? fg : bg, 3);
            CS_Deselect(dev);
        }
    }
}

static const uint8_t font5x7[128][5] = {
    [' ']  = {0x00,0x00,0x00,0x00,0x00},
    ['!']  = {0x00,0x00,0x5F,0x00,0x00},
    ['"']  = {0x00,0x07,0x00,0x07,0x00},
    ['#']  = {0x14,0x7F,0x14,0x7F,0x14},
    ['$']  = {0x24,0x2A,0x7F,0x2A,0x12},
    ['%']  = {0x23,0x13,0x08,0x64,0x62},
    ['&']  = {0x36,0x49,0x55,0x22,0x50},
    ['\''] = {0x00,0x05,0x03,0x00,0x00},
    ['(']  = {0x00,0x1C,0x22,0x41,0x00},
    [')']  = {0x00,0x41,0x22,0x1C,0x00},
    ['*']  = {0x14,0x08,0x3E,0x08,0x14},
    ['+']  = {0x08,0x08,0x3E,0x08,0x08},
    [',']  = {0x00,0x50,0x30,0x00,0x00},
    ['-']  = {0x08,0x08,0x08,0x08,0x08},
    ['.']  = {0x00,0x60,0x60,0x00,0x00},
    ['/']  = {0x20,0x10,0x08,0x04,0x02},
 
    ['0']  = {0x3E,0x51,0x49,0x45,0x3E},
    ['1']  = {0x00,0x42,0x7F,0x40,0x00},
    ['2']  = {0x42,0x61,0x51,0x49,0x46},
    ['3']  = {0x21,0x41,0x45,0x4B,0x31},
    ['4']  = {0x18,0x14,0x12,0x7F,0x10},
    ['5']  = {0x27,0x45,0x45,0x45,0x39},
    ['6']  = {0x3C,0x4A,0x49,0x49,0x30},
    ['7']  = {0x01,0x71,0x09,0x05,0x03},
    ['8']  = {0x36,0x49,0x49,0x49,0x36},
    ['9']  = {0x06,0x49,0x49,0x29,0x1E},
 
    [':']  = {0x00,0x36,0x36,0x00,0x00},
    [';']  = {0x00,0x56,0x36,0x00,0x00},
    ['<']  = {0x08,0x14,0x22,0x41,0x00},
    ['=']  = {0x14,0x14,0x14,0x14,0x14},
    ['>']  = {0x00,0x41,0x22,0x14,0x08},
    ['?']  = {0x02,0x01,0x51,0x09,0x06},
    ['@']  = {0x32,0x49,0x79,0x41,0x3E},
 
    ['A']  = {0x7E,0x11,0x11,0x11,0x7E},
    ['B']  = {0x7F,0x49,0x49,0x49,0x36},
    ['C']  = {0x3E,0x41,0x41,0x41,0x22},
    ['D']  = {0x7F,0x41,0x41,0x22,0x1C},
    ['E']  = {0x7F,0x49,0x49,0x49,0x41},
    ['F']  = {0x7F,0x09,0x09,0x09,0x01},
    ['G']  = {0x3E,0x41,0x49,0x49,0x7A},
    ['H']  = {0x7F,0x08,0x08,0x08,0x7F},
    ['I']  = {0x00,0x41,0x7F,0x41,0x00},
    ['J']  = {0x20,0x40,0x41,0x3F,0x01},
    ['K']  = {0x7F,0x08,0x14,0x22,0x41},
    ['L']  = {0x7F,0x40,0x40,0x40,0x40},
    ['M']  = {0x7F,0x02,0x0C,0x02,0x7F},
    ['N']  = {0x7F,0x04,0x08,0x10,0x7F},
    ['O']  = {0x3E,0x41,0x41,0x41,0x3E},
    ['P']  = {0x7F,0x09,0x09,0x09,0x06},
    ['Q']  = {0x3E,0x41,0x51,0x21,0x5E},
    ['R']  = {0x7F,0x09,0x19,0x29,0x46},
    ['S']  = {0x46,0x49,0x49,0x49,0x31},
    ['T']  = {0x01,0x01,0x7F,0x01,0x01},
    ['U']  = {0x3F,0x40,0x40,0x40,0x3F},
    ['V']  = {0x1F,0x20,0x40,0x20,0x1F},
    ['W']  = {0x7F,0x20,0x18,0x20,0x7F},
    ['X']  = {0x63,0x14,0x08,0x14,0x63},
    ['Y']  = {0x07,0x08,0x70,0x08,0x07},
    ['Z']  = {0x61,0x51,0x49,0x45,0x43},
 
    ['[']  = {0x00,0x7F,0x41,0x41,0x00},
    ['\\'] = {0x02,0x04,0x08,0x10,0x20},
    [']']  = {0x00,0x41,0x41,0x7F,0x00},
    ['^']  = {0x04,0x02,0x01,0x02,0x04},
    ['_']  = {0x40,0x40,0x40,0x40,0x40},
    ['`']  = {0x00,0x01,0x02,0x04,0x00},
 
    ['a']  = {0x20,0x54,0x54,0x54,0x78},
    ['b']  = {0x7F,0x48,0x44,0x44,0x38},
    ['c']  = {0x38,0x44,0x44,0x44,0x20},
    ['d']  = {0x38,0x44,0x44,0x48,0x7F},
    ['e']  = {0x38,0x54,0x54,0x54,0x18},
    ['f']  = {0x08,0x7E,0x09,0x01,0x02},
    ['g']  = {0x0C,0x52,0x52,0x52,0x3E},
    ['h']  = {0x7F,0x08,0x04,0x04,0x78},
    ['i']  = {0x00,0x44,0x7D,0x40,0x00},
    ['j']  = {0x20,0x40,0x44,0x3D,0x00},
    ['k']  = {0x7F,0x10,0x28,0x44,0x00},
    ['l']  = {0x00,0x41,0x7F,0x40,0x00},
    ['m']  = {0x7C,0x04,0x18,0x04,0x78},
    ['n']  = {0x7C,0x08,0x04,0x04,0x78},
    ['o']  = {0x38,0x44,0x44,0x44,0x38},
    ['p']  = {0x7C,0x14,0x14,0x14,0x08},
    ['q']  = {0x08,0x14,0x14,0x18,0x7C},
    ['r']  = {0x7C,0x08,0x04,0x04,0x08},
    ['s']  = {0x48,0x54,0x54,0x54,0x20},
    ['t']  = {0x04,0x3F,0x44,0x40,0x20},
    ['u']  = {0x3C,0x40,0x40,0x20,0x7C},
    ['v']  = {0x1C,0x20,0x40,0x20,0x1C},
    ['w']  = {0x3C,0x40,0x30,0x40,0x3C},
    ['x']  = {0x44,0x28,0x10,0x28,0x44},
    ['y']  = {0x0C,0x50,0x50,0x50,0x3C},
    ['z']  = {0x44,0x64,0x54,0x4C,0x44},
 
    ['{']  = {0x00,0x08,0x36,0x41,0x00},
    ['|']  = {0x00,0x00,0x7F,0x00,0x00},
    ['}']  = {0x00,0x41,0x36,0x08,0x00},
    ['~']  = {0x08,0x04,0x08,0x10,0x08}
};
 
void ILI9488_DrawChar(ILI9488_HandleTypeDef *dev,
                       uint16_t x, uint16_t y, char c,
                       uint16_t color565, uint16_t bg565, uint8_t size)
{
    uint8_t uc = (uint8_t)c;
    if (uc > 127) {
        uc = ' ';
    }
 
    for (uint8_t col = 0; col < 5; col++) {
        uint8_t line = font5x7[uc][col];
 
        uint8_t row = 0;
        while (row < 8) {
            bool set = (line >> row) & 0x01;
            uint8_t run_start = row;
            while (row < 8 && (((line >> row) & 0x01) != 0) == set) {
                row++;
            }
            uint8_t run_len = row - run_start;
            uint16_t drawColor = set ? color565 : bg565;
 
            ILI9488_FillRect(dev,
                              x + col * size,
                              y + run_start * size,
                              size,
                              run_len * size,
                              drawColor);
        }
    }

    ILI9488_FillRect(dev, x + 5 * size, y, size, 8 * size, bg565);
}
 
void ILI9488_Print(ILI9488_HandleTypeDef *dev,
                    uint16_t x, uint16_t y, const char *str,
                    uint16_t color565, uint16_t bg565, uint8_t size)
{
    while (*str) {
        ILI9488_DrawChar(dev, x, y, *str, color565, bg565, size);
        x += 6 * size;
        str++;
    }
}

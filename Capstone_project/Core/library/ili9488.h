/**
 * ili9488.h
 *
 * ILI9488 TFT LCD driver for STM32 (HAL), 4-wire SPI, 480x320.
 *
 * Notes on color depth:
 *   The ILI9488, when driven over 4-wire SPI, does not reliably support the
 *   16-bit (RGB565) pixel format advertised in its COLMOD register - many
 *   panels show corrupted / shifted colors in that mode. This driver instead
 *   uses the 18-bit (RGB666) pixel format, sending 3 bytes per pixel. The
 *   public API still takes normal RGB565 colors; conversion to 18-bit is
 *   done internally.
 *
 * Wiring (typical 4-wire SPI + separate D/C, RESET, and optional backlight):
 *   SCK  -> SPI SCK
 *   MOSI -> SPI MOSI (SDI)
 *   MISO -> not used (can be left unconnected on most modules)
 *   CS   -> GPIO (chip select), optional if handled by SPI hw NSS
 *   DC   -> GPIO (data/command select)
 *   RST  -> GPIO (reset)
 *   LED  -> GPIO or PWM (backlight), optional
 */

#ifndef ILI9488_H
#define ILI9488_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"   /* Brings in the correct HAL header for your STM32 family */
#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------------ */
/* Panel geometry                                                           */
/* ------------------------------------------------------------------------ */
#define ILI9488_NATIVE_WIDTH   320
#define ILI9488_NATIVE_HEIGHT  480

/* ------------------------------------------------------------------------ */
/* Rotation                                                                  */
/* ------------------------------------------------------------------------ */
typedef enum {
    ILI9488_ROTATION_0   = 0,  /* 320 x 480 (portrait)          */
    ILI9488_ROTATION_90  = 1,  /* 480 x 320 (landscape)         */
    ILI9488_ROTATION_180 = 2,  /* 320 x 480 (portrait, flipped) */
    ILI9488_ROTATION_270 = 3   /* 480 x 320 (landscape, flipped)*/
} ILI9488_Rotation_t;

/* ------------------------------------------------------------------------ */
/* Common RGB565 colors (handy defaults; convert automatically internally)  */
/* ------------------------------------------------------------------------ */
#define ILI9488_COLOR_BLACK       0x0000
#define ILI9488_COLOR_WHITE       0xFFFF
#define ILI9488_COLOR_RED         0xF800
#define ILI9488_COLOR_GREEN       0x07E0
#define ILI9488_COLOR_BLUE        0x001F
#define ILI9488_COLOR_YELLOW      0xFFE0
#define ILI9488_COLOR_CYAN        0x07FF
#define ILI9488_COLOR_MAGENTA     0xF81F
#define ILI9488_COLOR_ORANGE      0xFD20
#define ILI9488_COLOR_GRAY        0x8410
#define ILI9488_COLOR_DGRAY       0x7BEF   //  Dark gray   

/* ------------------------------------------------------------------------ */
/* Handle / config                                                          */
/* ------------------------------------------------------------------------ */
typedef struct {
    SPI_HandleTypeDef *hspi;

    GPIO_TypeDef *cs_port;    /* Set cs_port = NULL if CS is tied low / hw NSS */
    uint16_t      cs_pin;

    GPIO_TypeDef *dc_port;
    uint16_t      dc_pin;

    GPIO_TypeDef *rst_port;   /* Set rst_port = NULL if RESET is not wired to an MCU pin */
    uint16_t      rst_pin;

    GPIO_TypeDef *bl_port;    /* Set bl_port = NULL if backlight is not MCU-controlled */
    uint16_t      bl_pin;

    ILI9488_Rotation_t rotation;
    uint16_t width;           /* Current logical width, set by ILI9488_SetRotation()  */
    uint16_t height;          /* Current logical height, set by ILI9488_SetRotation() */
} ILI9488_HandleTypeDef;

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

/**
 * Initialize the ILI9488 controller and the passed handle.
 * SPI and GPIOs must already be initialized (e.g. via CubeMX) before calling.
 *
 * @param dev       Pointer to a handle you own; fill in hspi/cs/dc/rst/bl pins
 *                  before calling this function.
 * @param rotation  Initial screen rotation.
 */
void ILI9488_Init(ILI9488_HandleTypeDef *dev, ILI9488_Rotation_t rotation);

/** Hardware reset via RST pin (does nothing if rst_port == NULL). */
void ILI9488_HardwareReset(ILI9488_HandleTypeDef *dev);

/** Software reset via SPI command. */
void ILI9488_SoftwareReset(ILI9488_HandleTypeDef *dev);

/** Enter (on=true) or exit (on=false) display sleep mode. */
void ILI9488_Sleep(ILI9488_HandleTypeDef *dev, bool on);

/** Turn display output on/off (content stays in GRAM). */
void ILI9488_DisplayOn(ILI9488_HandleTypeDef *dev, bool on);

/** Turn backlight on/off, if bl_port is configured. */
void ILI9488_BacklightOn(ILI9488_HandleTypeDef *dev, bool on);

/** Change rotation at runtime; updates dev->width / dev->height. */
void ILI9488_SetRotation(ILI9488_HandleTypeDef *dev, ILI9488_Rotation_t rotation);

/** Invert colors on/off. */
void ILI9488_InvertColors(ILI9488_HandleTypeDef *dev, bool invert);

/**
 * Set the active drawing window (in current-rotation logical coordinates).
 * Subsequent pixel data sent will be written into this window, left-to-right
 * then top-to-bottom, wrapping at x1 back to x0.
 */
void ILI9488_SetAddressWindow(ILI9488_HandleTypeDef *dev,
                               uint16_t x0, uint16_t y0,
                               uint16_t x1, uint16_t y1);

/** Fill the entire screen with a single RGB565 color. */
void ILI9488_FillScreen(ILI9488_HandleTypeDef *dev, uint16_t color565);

/** Draw a single pixel. Slow if used in a loop for large areas; prefer FillRect. */
void ILI9488_DrawPixel(ILI9488_HandleTypeDef *dev, uint16_t x, uint16_t y, uint16_t color565);

/** Fill an axis-aligned rectangle with a solid RGB565 color. */
void ILI9488_FillRect(ILI9488_HandleTypeDef *dev,
                       uint16_t x, uint16_t y,
                       uint16_t w, uint16_t h,
                       uint16_t color565);

/** Draw an unfilled rectangle outline, 1px thick, RGB565 color. */
void ILI9488_DrawRect(ILI9488_HandleTypeDef *dev,
                       uint16_t x, uint16_t y,
                       uint16_t w, uint16_t h,
                       uint16_t color565);

/** Draw a horizontal line of length w starting at (x,y). */
void ILI9488_DrawFastHLine(ILI9488_HandleTypeDef *dev,
                            uint16_t x, uint16_t y, uint16_t w, uint16_t color565);

/** Draw a vertical line of length h starting at (x,y). */
void ILI9488_DrawFastVLine(ILI9488_HandleTypeDef *dev,
                            uint16_t x, uint16_t y, uint16_t h, uint16_t color565);

/** Draw an arbitrary line using Bresenham's algorithm. */
void ILI9488_DrawLine(ILI9488_HandleTypeDef *dev,
                       int16_t x0, int16_t y0,
                       int16_t x1, int16_t y1,
                       uint16_t color565);

/**
 * Blit a buffer of RGB565 pixels (w*h entries, row-major) to the screen at
 * (x, y). Useful for images, sprites, or offscreen-rendered framebuffers.
 */
void ILI9488_DrawImage(ILI9488_HandleTypeDef *dev,
                        uint16_t x, uint16_t y,
                        uint16_t w, uint16_t h,
                        const uint16_t *data565);

/**
 * Draw a 1-bit-per-pixel bitmap (e.g. a font glyph), MSB-first, byte-aligned
 * per row, using fg565 for set bits and bg565 for clear bits.
 */
void ILI9488_DrawBitmap1Bit(ILI9488_HandleTypeDef *dev,
                             uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h,
                             const uint8_t *bitmap,
                             uint16_t fg565, uint16_t bg565);


void ILI9488_Print(ILI9488_HandleTypeDef *dev,
                    uint16_t x, uint16_t y, const char *str,
                    uint16_t color565, uint16_t bg565, uint8_t size);

#ifdef __cplusplus
}
#endif

#endif /* ILI9488_H */

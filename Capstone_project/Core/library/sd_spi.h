#ifndef __SD_SPI_H__
#define __SD_SPI_H__

#include "main.h"  // or your specific STM32 family header
#include <stdint.h>

#define CMD0  (0)
#define CMD8  (8)
#define CMD9  (9)
#define CMD17 (17)
#define CMD24 (24)
#define CMD55 (55)
#define CMD58 (58)
#define ACMD41 (41)

typedef enum {
    SD_OK = 0,
    SD_ERROR
} SD_Status;

extern uint8_t card_initialized;

SD_Status SD_SPI_Init(void);
SD_Status SD_ReadBlocks(uint8_t *buff, uint32_t sector, uint32_t count);
SD_Status SD_WriteBlocks(const uint8_t *buff, uint32_t sector, uint32_t count);
SD_Status SD_ReadMultiBlocks(uint8_t *buff, uint32_t sector, uint32_t count);
SD_Status SD_WriteMultiBlocks(const uint8_t *buff, uint32_t sector, uint32_t count);
uint8_t sd_is_sdhc(void);

uint8_t SD_ReceiveByte(void);
void SD_TransmitByte(uint8_t data);
uint8_t SD_SendCommand(uint8_t cmd, uint32_t arg, uint8_t crc);

#endif // __SD_SPI_H__

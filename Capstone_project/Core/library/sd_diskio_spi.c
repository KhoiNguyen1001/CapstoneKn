/******************************************************************************
 *  File        : sd_diskio_spi.c
 *  Author      : ControllersTech
 *  Website     : https://controllerstech.com
 *  Date        : June 26, 2025
 *
 *  Description :
 *    This file is part of a custom STM32/Embedded tutorial series.
 *    For documentation, updates, and more examples, visit the website above.
 *
 *  Note :
 *    This code is written and maintained by ControllersTech.
 *    You are free to use and modify it for learning and development.
 ******************************************************************************/


#include "diskio.h"
#include "sd_spi.h"
#include "ff_gen_drv.h"


DSTATUS SD_disk_status(BYTE drv) {
    if (drv != 0)
        return STA_NOINIT;
    return 0;
}

DSTATUS SD_disk_initialize(BYTE drv) {
    if (drv != 0)
        return STA_NOINIT;

    return (SD_SPI_Init() == SD_OK) ? 0 : STA_NOINIT;
}

DRESULT SD_disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count) {
    if (pdrv != 0 || count == 0)
        return RES_PARERR;
    if (!card_initialized) return RES_NOTRDY;
    return (SD_ReadBlocks(buff, sector, count) == SD_OK) ? RES_OK : RES_ERROR;
}

DRESULT SD_disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count) {
    if (pdrv || !count) return RES_PARERR;
    if (!card_initialized) return RES_NOTRDY;
    return (SD_WriteBlocks(buff, sector, count) == SD_OK) ? RES_OK : RES_ERROR;
}

uint32_t SD_GetSectorCount(void) {
    uint8_t csd[16];
    uint8_t response;
    uint32_t sectors = 0;

    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);
    response = SD_SendCommand(CMD9, 0, 0xFF);
    if (response != 0x00) {
        HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
        SD_TransmitByte(0xFF);
        return 0;
    }

    uint8_t token;
    uint32_t timeout = HAL_GetTick() + 200;
    do {
        token = SD_ReceiveByte();
        if (token == 0xFE) break;
    } while (HAL_GetTick() < timeout);

    if (token != 0xFE) {
        HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
        SD_TransmitByte(0xFF);
        return 0;
    }

    for (int i = 0; i < 16; i++) csd[i] = SD_ReceiveByte();
    SD_ReceiveByte();  // CRC
    SD_ReceiveByte();
    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
    SD_TransmitByte(0xFF);

    uint8_t csd_structure = (csd[0] >> 6) & 0x03;

    if (csd_structure == 1) {
        // CSD v2 (SDHC/SDXC)
        uint32_t c_size = ((uint32_t)(csd[7] & 0x3F) << 16) |
                           ((uint32_t)csd[8] << 8) |
                           csd[9];
        sectors = (c_size + 1) * 1024;  // 512-byte sectors
    } else {
        // CSD v1 (SDSC)
        uint32_t c_size = ((uint32_t)(csd[6] & 0x03) << 10) |
                           ((uint32_t)csd[7] << 2) |
                           ((csd[8] >> 6) & 0x03);
        uint8_t c_size_mult = ((csd[9] & 0x03) << 1) | ((csd[10] >> 7) & 0x01);
        uint8_t read_bl_len = csd[5] & 0x0F;

        uint32_t block_len = 1UL << read_bl_len;
        uint32_t mult = 1UL << (c_size_mult + 2);
        uint32_t total_bytes = (c_size + 1) * mult * block_len;
        sectors = total_bytes / 512;
    }

    return sectors;
}

DRESULT SD_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv != 0)
        return RES_PARERR;

    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(DWORD *)buff = SD_GetSectorCount();
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

const Diskio_drvTypeDef SD_Driver = {
    SD_disk_initialize,
    SD_disk_status,
    SD_disk_read,
#if _USE_WRITE
    SD_disk_write,
#endif
#if _USE_IOCTL
    SD_disk_ioctl,
#endif
};

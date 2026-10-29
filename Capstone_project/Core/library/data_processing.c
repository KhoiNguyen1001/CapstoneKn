/**
 ******************************************************************************
 * @file           : data_processing.c
 * @brief          : 
 ******************************************************************************
 * @attention

 *
 ******************************************************************************
 */

#include "data_processing.h"
#include <stdio.h>
#include <string.h>
#include "sd_functions.h"

FRESULT csv_create_with_header(char *out_filename, size_t out_size)
{
  FIL fil;
  FRESULT res;
  static const char header[] = "TIMESTAMP,BUS_VOLT_V,CURRENT_A,CELL_VOLT_V,CELL_ID,CRC16\r\n";

  for (int i = 0; i < 1000; i++)
  {
    snprintf(out_filename, out_size, "LOG%03d.CSV", i);

    res = f_open(&fil, out_filename, FA_CREATE_NEW | FA_WRITE);
    if (res == FR_EXIST) continue;              // slot taken, try next
    if (res != FR_OK) {
	printf("heap free=%u min_ever=%u\r\n",
		   (unsigned)xPortGetFreeHeapSize(),
		   (unsigned)xPortGetMinimumEverFreeHeapSize());
      printf("csv: f_open(%s) failed, FRESULT=%d\r\n", out_filename, res);
      return res;                               // return the REAL error
    }

    UINT bw = 0;
    res = f_write(&fil, header, sizeof(header) - 1, &bw);
    FRESULT cres = f_close(&fil);               // data is actually flushed here
    if (res != FR_OK || cres != FR_OK || bw != sizeof(header) - 1) {
      printf("csv: header write=%d close=%d bw=%u\r\n", res, cres, bw);
      return (res != FR_OK) ? res : (cres != FR_OK ? cres : FR_DISK_ERR);
    }
    return FR_OK;
  }
  return FR_EXIST;                              // all 1000 names used
}

FRESULT csv_flush_buffer(const char *filename, const char *buf, size_t len)
{
  FIL file;
  UINT bw;
  FRESULT res;

  if (len == 0)
    return FR_OK; // nothing to write

  res = f_open(&file, filename, FA_OPEN_ALWAYS | FA_WRITE);
  if (res != FR_OK)
    return res;

  res = f_lseek(&file, f_size(&file));
  if (res != FR_OK)
  {
    f_close(&file);
    return res;
  }

  res = f_write(&file, buf, (UINT)len, &bw);
  f_close(&file);

  return (res == FR_OK && bw == (UINT)len) ? FR_OK : FR_DISK_ERR;
}

/**
 * Appends one CSV row directly into log_buffer at the given offset.
 * Returns the new offset (buffer length) on success, or the old offset
 * unchanged if the row wouldn't fit (caller should flush first, then retry).
 */
size_t csv_append_sample(char *buf, size_t buf_size, size_t current_offset, LogRecord_t *records)
{
  char *start = buf + current_offset;
  size_t avail = buf_size - current_offset;

  int len = snprintf(start, avail,
                    // "%lu.%03u,%.7f,%u\r\n",
                    "%lu.%03u,%.5f,%.5f,%.7f,%u\r\n",
                    (unsigned long)records->timestamp,
                    records->timestamp_ms,
                    records->bus_voltage_V,
                    records->current_A,
                    records->cell_voltage_V,
                    records->cell_id
                    );

  if (len < 0 || (size_t)len >= (buf_size - current_offset))
  {
    // Wouldn't fit — don't corrupt/truncate, just report "no change"
    return current_offset;
  }

  // uint16_t crc = CalculateCRC16((const uint8_t *)start, len);

  // int len2 = snprintf(start + len, avail - len, ",%04X\r\n", crc);
  // if (len2 < 0 || (size_t)len2 >= avail - len)
  // {
  //   // Wouldn't fit — don't corrupt/truncate, just report "no change"
  //   return current_offset;
  // }

  return current_offset + (size_t)(len);// + len2);
}

uint16_t CalculateCRC16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < length; i++)
    {
        crc ^= (uint16_t)data[i] << 8;

        for (uint8_t bit = 0; bit < 8; bit++)
        {
            if (crc & 0x8000)
            {
                crc = (crc << 1) ^ 0x1021;
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

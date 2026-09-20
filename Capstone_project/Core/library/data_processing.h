#ifndef DATA_PROCESSING_H
#define DATA_PROCESSING_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "fatfs.h"

FRESULT csv_create_with_header(char *out_filename, size_t out_size);
FRESULT csv_flush_buffer(const char *filename, const char *buf, size_t len);
size_t csv_append_sample(char *buf, size_t buf_size, size_t current_offset, LogRecord_t *records);
uint16_t CalculateCRC16(const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* DATA_PROCESSING_H */
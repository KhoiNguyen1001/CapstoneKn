#ifndef RTC_UART_H
#define RTC_UART_H

#include "../library/ds3231.h"

#ifdef __cplusplus
extern "C" {
#endif

void rtc_uart_init(void);
void rtc_uart_process(DS3231_TimeDate *t);

#ifdef __cplusplus
}
#endif

#endif /* RTC_UART_H */
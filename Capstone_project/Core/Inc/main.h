/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32l4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
typedef struct
{
  uint32_t timestamp;
  uint16_t timestamp_ms;
  float bus_voltage_V;  // 0-20V
  float current_A;      // 0-30A
  float cell_voltage_V; // ~4.xxxxx
  uint8_t cell_id;      // 1-4
} LogRecord_t;

typedef struct
{
  uint8_t ads1115_status; //Hal _status
  uint8_t ina226_status; 
  uint8_t sd_card_status; 
  uint8_t lcd_page;
  uint8_t ds3231_status;
  uint8_t mppt_status;
} module_status_t;

enum lcd_page_t{
  ABOUT_PAGE = 0,
  MEASURE_PAGE = 1,
  SETTING_PAGE = 2,
};
/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define USART_TX_Pin GPIO_PIN_2
#define USART_TX_GPIO_Port GPIOA
#define USART_RX_Pin GPIO_PIN_3
#define USART_RX_GPIO_Port GPIOA
#define RS485_RE_Pin GPIO_PIN_4
#define RS485_RE_GPIO_Port GPIOA
#define RS485_DE_Pin GPIO_PIN_0
#define RS485_DE_GPIO_Port GPIOB
#define MCU_RL1_Pin GPIO_PIN_11
#define MCU_RL1_GPIO_Port GPIOB
#define MCU_RL2_Pin GPIO_PIN_12
#define MCU_RL2_GPIO_Port GPIOB
#define TFT_RST_Pin GPIO_PIN_7
#define TFT_RST_GPIO_Port GPIOC
#define INA226_INT_Pin GPIO_PIN_9
#define INA226_INT_GPIO_Port GPIOC
#define TFT_DC_Pin GPIO_PIN_9
#define TFT_DC_GPIO_Port GPIOA
#define SD_CS_Pin GPIO_PIN_10
#define SD_CS_GPIO_Port GPIOA
#define MCU_RL3_Pin GPIO_PIN_11
#define MCU_RL3_GPIO_Port GPIOA
#define MCU_RL4_Pin GPIO_PIN_12
#define MCU_RL4_GPIO_Port GPIOA
#define TMS_Pin GPIO_PIN_13
#define TMS_GPIO_Port GPIOA
#define TCK_Pin GPIO_PIN_14
#define TCK_GPIO_Port GPIOA
#define ADS1115_INT_Pin GPIO_PIN_15
#define ADS1115_INT_GPIO_Port GPIOA
#define TOUCH_INT_Pin GPIO_PIN_15
#define TOUCH_INT_GPIO_Port GPIOB
#define SPI3_CS_Pin GPIO_PIN_2
#define SPI3_CS_GPIO_Port GPIOD
#define TFT_BL_Pin GPIO_PIN_5
#define TFT_BL_GPIO_Port GPIOB
#define TFT_CS_Pin GPIO_PIN_6
#define TFT_CS_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
#define LOAD_CONTROL_RELAY1 1
#define LOAD_CONTROL_RELAY2 2
#define LOAD_CONTROL_RELAY3 3
#define LOAD_CONTROL_RELAY4 4
#define LOAD_CONTROL_RELAY_ALL 5
#define RELAY_ON GPIO_PIN_RESET
#define RELAY_OFF GPIO_PIN_SET
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "../library/ads1115.h"
#include "../library/INA226.h"
#include "../library/ili9488.h"
#include "../library/touch.h"
#include "../library/sd_functions.h"
#include "../library/data_processing.h"
#include "../library/DS3231.h"
#include "../rs485_library/epever_mppt.h"
#include "../debug_rtc/rtc_uart.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c2;
I2C_HandleTypeDef hi2c3;

SPI_HandleTypeDef hspi1;
SPI_HandleTypeDef hspi3;
DMA_HandleTypeDef hdma_spi1_tx;

UART_HandleTypeDef huart4;
UART_HandleTypeDef huart2;

/* Definitions for LowPriority */
osThreadId_t LowPriorityHandle;
const osThreadAttr_t LowPriority_attributes = {
  .name = "LowPriority",
  .stack_size = 2048 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for DataAcquisition */
osThreadId_t DataAcquisitionHandle;
const osThreadAttr_t DataAcquisition_attributes = {
  .name = "DataAcquisition",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for myMutex01 */
osMutexId_t myMutex01Handle;
const osMutexAttr_t myMutex01_attributes = {
  .name = "myMutex01"
};
/* USER CODE BEGIN PV */
float shunt_v;
module_status_t module_status;
ILI9488_HandleTypeDef lcd;
Touch_Handle_t touch_lcd;
ads1115_t ads1115a;
INA226_HandleTypeDef ina226;
uint16_t ina226_alert_status;
DS3231_HandleTypeDef DS3231_Handler;
DS3231_TimeDate td;

LogRecord_t LogRecord_data;
LogRecord_t LogRecord_data_LCD;

#define DS3231_ADDRESS 0x68 << 1
#define GT911_ADDRESS 0x5D << 1
#define LOG_RECORD_COUNT 475
#define LOG_BUFFER_SIZE 12288*2 // 12 KB — up to 250 rows/ 1sample=40byte

char log_buffer_A[LOG_BUFFER_SIZE];
char log_buffer_B[LOG_BUFFER_SIZE];
char buffer[30];
char current_filename[16];

uint32_t sample_count = 0;
char *active_buf = log_buffer_A;
char *flush_buf = NULL;
size_t active_len = 0;
size_t flush_len = 0;

const float calib_coeff [4][2] = {
  {-0.00384422, 0.51045767},
  {-0.00305992, 0.51049206}, 
  {-0.00310162, 0.51049930},
  {0.000087366, 0.51055476}, //Channel 4 -> [3]
};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2C2_Init(void);
static void MX_I2C3_Init(void);
static void MX_SPI3_Init(void);
static void MX_UART4_Init(void);
void Low_priority(void *argument);
void Data_Acquisition(void *argument);

/* USER CODE BEGIN PFP */
void Set_SPI_Speed(uint32_t prescaler);
void load_control(uint8_t relay, GPIO_PinState state);
void LCD_TFT_init(void);
void display_measure_page(void);
void display_about_page(void);

void on_new_sample(LogRecord_t *records);
void logging_task(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int _write(int file, char *ptr, int len)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    return len;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SPI1_Init();
  MX_USART2_UART_Init();
  //MX_FATFS_Init();
  MX_I2C1_Init();
  MX_I2C2_Init();
  MX_I2C3_Init();
  MX_SPI3_Init();
  MX_UART4_Init();
  /* USER CODE BEGIN 2 */
  load_control(LOAD_CONTROL_RELAY_ALL, RELAY_OFF);
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET); /* Deselect TFT */
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);   /* Deselect SD Card */
  HAL_Delay(200);

  LCD_TFT_init();
  Touch_Init(&touch_lcd, &hi2c2, GT911_ADDRESS);
  module_status.ads1115_status = ADS1115_Init(&ads1115a, &hi2c3, ADS1115_ADDR_GND, ADS_REG_CONFIG_PGA_2_048V, ADS_REG_CONFIG_DR_860SPS);
  module_status.ina226_status = INA226_Init(&ina226, &hi2c1, INA226_ADDR, 0.0025, 30);
  module_status.ina226_status = INA226_SetAlertFunction(&ina226, INA226_MASK_CNVR);
  module_status.ina226_status = INA226_ReadMaskEnable(&ina226, &ina226_alert_status);
  DS3231_Init(&DS3231_Handler, &hi2c3, DS3231_ADDRESS);

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();
  /* Create the mutex(es) */
  /* creation of myMutex01 */
  myMutex01Handle = osMutexNew(&myMutex01_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of LowPriority */
  LowPriorityHandle = osThreadNew(Low_priority, NULL, &LowPriority_attributes);

  /* creation of DataAcquisition */
  DataAcquisitionHandle = osThreadNew(Data_Acquisition, NULL, &DataAcquisition_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00F12981;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief I2C2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C2_Init(void)
{

  /* USER CODE BEGIN I2C2_Init 0 */

  /* USER CODE END I2C2_Init 0 */

  /* USER CODE BEGIN I2C2_Init 1 */

  /* USER CODE END I2C2_Init 1 */
  hi2c2.Instance = I2C2;
  hi2c2.Init.Timing = 0x10D19CE4;
  hi2c2.Init.OwnAddress1 = 0;
  hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c2.Init.OwnAddress2 = 0;
  hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c2, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C2_Init 2 */

  /* USER CODE END I2C2_Init 2 */

}

/**
  * @brief I2C3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C3_Init(void)
{

  /* USER CODE BEGIN I2C3_Init 0 */

  /* USER CODE END I2C3_Init 0 */

  /* USER CODE BEGIN I2C3_Init 1 */

  /* USER CODE END I2C3_Init 1 */
  hi2c3.Instance = I2C3;
  hi2c3.Init.Timing = 0x00F12981;
  hi2c3.Init.OwnAddress1 = 0;
  hi2c3.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c3.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c3.Init.OwnAddress2 = 0;
  hi2c3.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c3.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c3.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c3) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c3, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c3, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C3_Init 2 */

  /* USER CODE END I2C3_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief SPI3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI3_Init(void)
{

  /* USER CODE BEGIN SPI3_Init 0 */

  /* USER CODE END SPI3_Init 0 */

  /* USER CODE BEGIN SPI3_Init 1 */

  /* USER CODE END SPI3_Init 1 */
  /* SPI3 parameter configuration*/
  hspi3.Instance = SPI3;
  hspi3.Init.Mode = SPI_MODE_MASTER;
  hspi3.Init.Direction = SPI_DIRECTION_2LINES;
  hspi3.Init.DataSize = SPI_DATASIZE_4BIT;
  hspi3.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi3.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi3.Init.NSS = SPI_NSS_SOFT;
  hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi3.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi3.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi3.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi3.Init.CRCPolynomial = 7;
  hspi3.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi3.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI3_Init 2 */

  /* USER CODE END SPI3_Init 2 */

}

/**
  * @brief UART4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_UART4_Init(void)
{

  /* USER CODE BEGIN UART4_Init 0 */

  /* USER CODE END UART4_Init 0 */

  /* USER CODE BEGIN UART4_Init 1 */

  /* USER CODE END UART4_Init 1 */
  huart4.Instance = UART4;
  huart4.Init.BaudRate = 115200;
  huart4.Init.WordLength = UART_WORDLENGTH_8B;
  huart4.Init.StopBits = UART_STOPBITS_1;
  huart4.Init.Parity = UART_PARITY_NONE;
  huart4.Init.Mode = UART_MODE_TX_RX;
  huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart4.Init.OverSampling = UART_OVERSAMPLING_16;
  huart4.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart4.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN UART4_Init 2 */

  /* USER CODE END UART4_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */
  HAL_NVIC_SetPriority(USART2_IRQn, 0, 4);
  HAL_NVIC_EnableIRQ(USART2_IRQn);
  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel3_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, RS485_RE_Pin|TFT_DC_Pin|MCU_RL3_Pin|MCU_RL4_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, RS485_DE_Pin|MCU_RL1_Pin|MCU_RL2_Pin|TFT_BL_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SPI3_CS_GPIO_Port, SPI3_CS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : RS485_RE_Pin TFT_DC_Pin MCU_RL3_Pin MCU_RL4_Pin */
  GPIO_InitStruct.Pin = RS485_RE_Pin|TFT_DC_Pin|MCU_RL3_Pin|MCU_RL4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : RS485_DE_Pin MCU_RL1_Pin MCU_RL2_Pin TFT_BL_Pin
                           TFT_CS_Pin */
  GPIO_InitStruct.Pin = RS485_DE_Pin|MCU_RL1_Pin|MCU_RL2_Pin|TFT_BL_Pin
                          |TFT_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : TFT_RST_Pin */
  GPIO_InitStruct.Pin = TFT_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(TFT_RST_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : INA226_INT_Pin */
  GPIO_InitStruct.Pin = INA226_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(INA226_INT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : SD_CS_Pin */
  GPIO_InitStruct.Pin = SD_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(SD_CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : ADS1115_INT_Pin */
  GPIO_InitStruct.Pin = ADS1115_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(ADS1115_INT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : TOUCH_INT_Pin */
  GPIO_InitStruct.Pin = TOUCH_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(TOUCH_INT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : SPI3_CS_Pin */
  GPIO_InitStruct.Pin = SPI3_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SPI3_CS_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == ADS1115_INT_Pin)
  {
    ads1115_data_ready = true;
  }
  if (GPIO_Pin == INA226_INT_Pin)
  {
    INA226_data_ready = true;
  }
}

void Set_SPI_Speed(uint32_t prescaler)
{
  __HAL_SPI_DISABLE(&hspi1);
  hspi1.Init.BaudRatePrescaler = prescaler;
  HAL_SPI_Init(&hspi1);
  __HAL_SPI_ENABLE(&hspi1);
}

void load_control(uint8_t relay, GPIO_PinState state)
{
  switch (relay)
  {
  case LOAD_CONTROL_RELAY1:
    HAL_GPIO_WritePin(MCU_RL1_GPIO_Port, MCU_RL1_Pin, state);
    break;
  case LOAD_CONTROL_RELAY2:
    HAL_GPIO_WritePin(MCU_RL2_GPIO_Port, MCU_RL2_Pin, state);
    break;
  case LOAD_CONTROL_RELAY3:
    HAL_GPIO_WritePin(MCU_RL3_GPIO_Port, MCU_RL3_Pin, state);
    break;
  case LOAD_CONTROL_RELAY4:
    HAL_GPIO_WritePin(MCU_RL4_GPIO_Port, MCU_RL4_Pin, state);
    break;
  case LOAD_CONTROL_RELAY_ALL:
    HAL_GPIO_WritePin(MCU_RL1_GPIO_Port, MCU_RL1_Pin, state);
    HAL_GPIO_WritePin(MCU_RL2_GPIO_Port, MCU_RL2_Pin, state);
    HAL_GPIO_WritePin(MCU_RL3_GPIO_Port, MCU_RL3_Pin, state);
    HAL_GPIO_WritePin(MCU_RL4_GPIO_Port, MCU_RL4_Pin, state);
    break;
  default:
    break;
  }
}

void LCD_TFT_init(void)
{
  lcd.hspi = &hspi1;

  lcd.cs_port = TFT_CS_GPIO_Port;
  lcd.cs_pin = TFT_CS_Pin;
  lcd.dc_port = TFT_DC_GPIO_Port;
  lcd.dc_pin = TFT_DC_Pin;
  lcd.rst_port = TFT_RST_GPIO_Port;
  lcd.rst_pin = TFT_RST_Pin;
  lcd.bl_port = TFT_BL_GPIO_Port;
  lcd.bl_pin = TFT_BL_Pin;

  ILI9488_Init(&lcd, ILI9488_ROTATION_0);
  ILI9488_FillScreen(&lcd, ILI9488_COLOR_BLACK);

  LCD_draw_tab_button(&lcd, 5, 90,  "ABOUT",   false);
  LCD_draw_tab_button(&lcd, 100, 100, "MEASURE", true);
  LCD_draw_tab_button(&lcd, 205, 100, "SETTING", false);

  module_status.lcd_page = MEASURE_PAGE;
  display_measure_page();
}

void display_measure_page(void)
{
  ILI9488_FillRect(&lcd, 5, 45, 310, 2, ILI9488_COLOR_DGRAY);
  ILI9488_FillRect(&lcd, 5, 135, 310, 2, ILI9488_COLOR_DGRAY);
  ILI9488_FillRect(&lcd, 5, 295, 310, 2, ILI9488_COLOR_DGRAY);
  ILI9488_FillRect(&lcd, 5, 420, 310, 2, ILI9488_COLOR_DGRAY);

  ILI9488_Print(&lcd, 15, 60, "CURRENT:", ILI9488_COLOR_YELLOW, ILI9488_COLOR_BLACK, 2);
  ILI9488_Print(&lcd, 15, 100, "BUS VOLT:", ILI9488_COLOR_CYAN, ILI9488_COLOR_BLACK, 2);
  ILI9488_Print(&lcd, 15, 150, "Cell 1:", ILI9488_COLOR_GREEN, ILI9488_COLOR_BLACK, 2);
  ILI9488_Print(&lcd, 15, 185, "Cell 2:", ILI9488_COLOR_GREEN, ILI9488_COLOR_BLACK, 2);
  ILI9488_Print(&lcd, 15, 220, "Cell 3:", ILI9488_COLOR_GREEN, ILI9488_COLOR_BLACK, 2);
  ILI9488_Print(&lcd, 15, 255, "Cell 4:", ILI9488_COLOR_GREEN, ILI9488_COLOR_BLACK, 2);
  ILI9488_Print(&lcd, 15, 315, "Time:", ILI9488_COLOR_GREEN, ILI9488_COLOR_BLACK, 2);

}

void display_about_page(void)
{
  ILI9488_Print(&lcd, 15, 60, "SOLAR BATTERY MONITOR", ILI9488_COLOR_CYAN, ILI9488_COLOR_BLACK, 2);
  ILI9488_FillRect(&lcd, 5, 80, 310, 2, ILI9488_COLOR_DGRAY);

  ILI9488_Print(&lcd, 15, 100, "Firmware version: 1.01", ILI9488_COLOR_CYAN, ILI9488_COLOR_BLACK, 2);
  ILI9488_Print(&lcd, 15, 135, "ADS1115:", ILI9488_COLOR_YELLOW, ILI9488_COLOR_BLACK, 2);
  ILI9488_Print(&lcd, 15, 170, "INA226:", ILI9488_COLOR_YELLOW, ILI9488_COLOR_BLACK, 2);
  ILI9488_Print(&lcd, 15, 205, "SD CARD:", ILI9488_COLOR_GREEN, ILI9488_COLOR_BLACK, 2);
  ILI9488_Print(&lcd, 15, 240, "Time:", ILI9488_COLOR_GREEN, ILI9488_COLOR_BLACK, 2);
  ILI9488_Print(&lcd, 15, 275, "SD STATUS:", ILI9488_COLOR_WHITE, ILI9488_COLOR_BLACK, 2);

  if (module_status.sd_card_status == FR_OK)
    ILI9488_Print(&lcd, 160, 275, "GOOD   ", ILI9488_COLOR_GREEN, ILI9488_COLOR_BLACK, 2);
  else
    ILI9488_Print(&lcd, 160, 275, "ERROR   ", ILI9488_COLOR_RED, ILI9488_COLOR_BLACK, 2);
}

void on_new_sample(LogRecord_t *records) // Run in data acquisitions thread
{
  sample_count++;
  // printf("%lu.%03u,%.5f,%.5f,%u,%.7f\r\n",
	// 	  records->timestamp, records->timestamp_ms,
	// 	  records->bus_voltage_V, records->current_A,
	// 	  records->cell_id, records->cell_voltage_V);
  size_t new_len = csv_append_sample(active_buf, LOG_BUFFER_SIZE, active_len, records);

  if ((new_len == active_len) || (sample_count > 1000))
  {
    osMutexAcquire(myMutex01Handle, osWaitForever);
    if (flush_buf == NULL)
    {
      flush_buf = active_buf;
      flush_len = active_len;
      active_buf = (active_buf == log_buffer_A) ? log_buffer_B : log_buffer_A;
      active_len = 0;
      sample_count = 0;
      // retry the sample into the now-empty active buffer
      active_len = csv_append_sample(active_buf, LOG_BUFFER_SIZE, 0, records);
    }
    osMutexRelease(myMutex01Handle);
    //printf("%.*s", (int)flush_len, flush_buf);
  }
  else
  {
    active_len = new_len;
  }

  LogRecord_data_LCD.timestamp = LogRecord_data.timestamp;
  if (records->cell_id != 0) // make sure we only update the LCD with cell voltage if it's a cell voltage record
  {
    LogRecord_data_LCD.cell_id = records->cell_id;
    LogRecord_data_LCD.cell_voltage_V = records->cell_voltage_V;
  }
  else { // if it's a bus voltage/current record, update the LCD with those values
    LogRecord_data_LCD.bus_voltage_V = records->bus_voltage_V;
    LogRecord_data_LCD.current_A = records->current_A;
  }
  memset(records,0,sizeof(LogRecord_t));// clear record
}

void logging_task(void) // Run in low priority thread
{
  char *local_flush_buf = NULL;
  size_t local_flush_len = 0;

  osMutexAcquire(myMutex01Handle, osWaitForever);
  if (flush_buf != NULL && flush_len > 0)
  {
    local_flush_buf = flush_buf;
    local_flush_len = flush_len;
  }
  osMutexRelease(myMutex01Handle);

  if (local_flush_buf != NULL && local_flush_len > 0)
  {
    csv_flush_buffer(current_filename, local_flush_buf, local_flush_len);
    osMutexAcquire(myMutex01Handle, osWaitForever);
    flush_buf = NULL;   // only clear after the write actually completes
    osMutexRelease(myMutex01Handle);
  }
}  
/* USER CODE END 4 */

/* USER CODE BEGIN Header_Low_priority */
/**
  * @brief  Function implementing the LowPriority thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_Low_priority */
void Low_priority(void *argument)
{
  /* USER CODE BEGIN 5 */
  static uint8_t LCD_refresh_count = 0;
  Set_SPI_Speed(SPI_BAUDRATEPRESCALER_128);
  HAL_Delay(500);
  sd_mount();
  Set_SPI_Speed(SPI_BAUDRATEPRESCALER_16);
  module_status.sd_card_status = csv_create_with_header(current_filename, sizeof(current_filename));

  uint32_t sod;
  rtc_uart_init();
  while(true)
  {
    rtc_uart_process(&td);
    logging_task();

    LCD_refresh_count++;
    if (LCD_refresh_count > 100)
    {
      LCD_refresh_count = 0;
      if(module_status.lcd_page == MEASURE_PAGE)
      {
        sprintf(buffer, "%.7f A   ", LogRecord_data_LCD.current_A);
        ILI9488_Print(&lcd, 160, 60, buffer, ILI9488_COLOR_WHITE, ILI9488_COLOR_BLACK, 2);
        sprintf(buffer, "%.7f V   ", LogRecord_data_LCD.bus_voltage_V);
        ILI9488_Print(&lcd, 160, 100, buffer, ILI9488_COLOR_WHITE, ILI9488_COLOR_BLACK, 2);
        sod = LogRecord_data_LCD.timestamp % 86400u;
        sprintf(buffer, "%02d/%02d/%04d  %02d:%02d:%02d", td.day, td.month, td.year, td.hours,(int)((sod % 3600) / 60), (int)(sod % 60));
        ILI9488_Print(&lcd, 80, 315, buffer, ILI9488_COLOR_WHITE, ILI9488_COLOR_BLACK, 2);

        sprintf(buffer, "%.7f V   ", LogRecord_data_LCD.cell_voltage_V);
        switch (LogRecord_data_LCD.cell_id) {
          case 1:
            ILI9488_Print(&lcd,120, 150, buffer, ILI9488_COLOR_WHITE, ILI9488_COLOR_BLACK, 2);
            break;
          case 2:
            ILI9488_Print(&lcd,120, 185, buffer, ILI9488_COLOR_WHITE, ILI9488_COLOR_BLACK, 2);
            break;
          case 3:
            ILI9488_Print(&lcd,120, 220, buffer, ILI9488_COLOR_WHITE, ILI9488_COLOR_BLACK, 2);
            break;
          default:
            ILI9488_Print(&lcd,120, 255, buffer, ILI9488_COLOR_WHITE, ILI9488_COLOR_BLACK, 2);
            break;
        }
      }
      else if(module_status.lcd_page == ABOUT_PAGE)
      {
        sod = LogRecord_data_LCD.timestamp % 86400u;
        sprintf(buffer, "%02d/%02d/%04d  %02d:%02d:%02d", td.day, td.month, td.year, td.hours,(int)((sod % 3600) / 60), (int)(sod % 60));
        ILI9488_Print(&lcd, 80, 240, buffer, ILI9488_COLOR_WHITE, ILI9488_COLOR_BLACK, 2);
      }
    }

    if(LCD_refresh_count%50 == 0)
    {
      if(touch_process(&touch_lcd, &module_status.lcd_page))
      {
        LCD_refresh_count = 0;
        /// reinit page
        for (int i = 0; i < 3; i++)
        {
          LCD_draw_tab_button(&lcd, tabs[i].x, tabs[i].w, tabs[i].text, i == module_status.lcd_page);
        }
        ILI9488_FillRect(&lcd, 0, 60, 480, 273, ILI9488_COLOR_BLACK);
        if(module_status.lcd_page == MEASURE_PAGE)
        {
          display_measure_page();
        }
        if(module_status.lcd_page == ABOUT_PAGE)  display_about_page();
      }
    }
    osDelay(2);
  }
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_Data_Acquisition */
/**
* @brief Function implementing the DataAcquisition thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Data_Acquisition */
void Data_Acquisition(void *argument)
{
  /* USER CODE BEGIN Data_Acquisition */
  static uint8_t cell_id = 8;
  static uint32_t os_time_pre = 0;
  uint32_t os_time, os_time_elapsed;
  float discard_measusement;
  module_status.channel_calib_settings = 4; // default channel for calibration
  module_status.channel_calib_settings_cached = 4; // default channel for calibration

  DS3231_GetTimeDate(&DS3231_Handler, &td);
  os_time = osKernelGetTickCount();
  os_time_pre = os_time;
  
  ADS1115_StartConversion(&ads1115a, ADS1115_CH_3);
  while(true)
  {
    os_time = osKernelGetTickCount();
    os_time_elapsed = os_time - os_time_pre;
    LogRecord_data.timestamp = DS3231_ToUnixTimestamp(&td) + (uint32_t)(os_time_elapsed/1000);
    LogRecord_data.timestamp_ms = (uint16_t)(os_time_elapsed%1000);

    if (ADS1115_IsDataReady())
    {
      if(cell_id%2 == 1) // discard first sample after switch channel
      {
	      LogRecord_data.cell_id = cell_id/2 + 1;
        
        ADS1115_ReadResult(&ads1115a, &discard_measusement);
        LogRecord_data.cell_voltage_V = (discard_measusement - calib_coeff[LogRecord_data.cell_id - 1][0]) / calib_coeff[LogRecord_data.cell_id - 1][1];
        //ADS1115_ReadResult(&ads1115a, &LogRecord_data.cell_voltage_V);
        on_new_sample(&LogRecord_data);
      }
      else ADS1115_ReadResult(&ads1115a, &discard_measusement);
      cell_id--;

      //Calibration code modification BEGIN
      if(module_status.channel_calib_settings != 0)
      {
        cell_id = module_status.channel_calib_settings*2 - 1; //7; // fixed cell_id
        //cell_id_prev = cell_id + 1; //8;

        if(module_status.channel_calib_settings != module_status.channel_calib_settings_cached)
        {
          module_status.channel_calib_settings_cached = module_status.channel_calib_settings;
          cell_id = module_status.channel_calib_settings*2;
        }
      }
      //Calibration code modification END
      switch (cell_id)
      {
      case 2:
        ADS1115_StartConversion(&ads1115a, ADS1115_CH_0);
        break;
      case 4:
        ADS1115_StartConversion(&ads1115a, ADS1115_CH_1);
        break;
      case 6:
        ADS1115_StartConversion(&ads1115a, ADS1115_CH_2);
        break;
      case 0:
	    cell_id = 8; //if cell_id = 0 -> cell_id = 4
        ADS1115_StartConversion(&ads1115a, ADS1115_CH_3);
        break;
      }
    }

     if (INA226_data_ready)
     {
       INA226_data_ready = false;
       INA226_ReadMaskEnable(&ina226, &ina226_alert_status); // status & INA226_MASK_CVRF
      //  INA226_ReadShuntVoltage(&ina226, &shunt_v);
       INA226_ReadCurrent(&ina226, &LogRecord_data.current_A);
       INA226_ReadBusVoltage(&ina226, &LogRecord_data.bus_voltage_V);
      
       // Logging data (voltage and current)
       LogRecord_data.timestamp = DS3231_ToUnixTimestamp(&td) + (uint32_t)(os_time_elapsed/1000);
       LogRecord_data.timestamp_ms = (uint16_t)(os_time_elapsed%1000);
       on_new_sample(&LogRecord_data);
     }

    if(os_time_elapsed >= 900000)
    {
      os_time_pre = os_time;
      DS3231_GetTimeDate(&DS3231_Handler, &td);
    }
    osDelay(1);
  }
  /* USER CODE END Data_Acquisition */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

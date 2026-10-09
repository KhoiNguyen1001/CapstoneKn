################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/library/DS3231.c \
../Core/library/INA226.c \
../Core/library/ads1115.c \
../Core/library/data_processing.c \
../Core/library/ili9488.c \
../Core/library/sd_benchmark.c \
../Core/library/sd_diskio_spi.c \
../Core/library/sd_functions.c \
../Core/library/sd_spi.c 

OBJS += \
./Core/library/DS3231.o \
./Core/library/INA226.o \
./Core/library/ads1115.o \
./Core/library/data_processing.o \
./Core/library/ili9488.o \
./Core/library/sd_benchmark.o \
./Core/library/sd_diskio_spi.o \
./Core/library/sd_functions.o \
./Core/library/sd_spi.o 

C_DEPS += \
./Core/library/DS3231.d \
./Core/library/INA226.d \
./Core/library/ads1115.d \
./Core/library/data_processing.d \
./Core/library/ili9488.d \
./Core/library/sd_benchmark.d \
./Core/library/sd_diskio_spi.d \
./Core/library/sd_functions.d \
./Core/library/sd_spi.d 


# Each subdirectory must supply rules for building sources it contributes
Core/library/%.o Core/library/%.su Core/library/%.cyclo: ../Core/library/%.c Core/library/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32L476xx -c -I../FATFS/Target -I../FATFS/App -I../Core/Inc -I../Drivers/STM32L4xx_HAL_Driver/Inc -I../Drivers/STM32L4xx_HAL_Driver/Inc/Legacy -I../Middlewares/Third_Party/FatFs/src -I../Drivers/CMSIS/Device/ST/STM32L4xx/Include -I../Drivers/CMSIS/Include -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-library

clean-Core-2f-library:
	-$(RM) ./Core/library/DS3231.cyclo ./Core/library/DS3231.d ./Core/library/DS3231.o ./Core/library/DS3231.su ./Core/library/INA226.cyclo ./Core/library/INA226.d ./Core/library/INA226.o ./Core/library/INA226.su ./Core/library/ads1115.cyclo ./Core/library/ads1115.d ./Core/library/ads1115.o ./Core/library/ads1115.su ./Core/library/data_processing.cyclo ./Core/library/data_processing.d ./Core/library/data_processing.o ./Core/library/data_processing.su ./Core/library/ili9488.cyclo ./Core/library/ili9488.d ./Core/library/ili9488.o ./Core/library/ili9488.su ./Core/library/sd_benchmark.cyclo ./Core/library/sd_benchmark.d ./Core/library/sd_benchmark.o ./Core/library/sd_benchmark.su ./Core/library/sd_diskio_spi.cyclo ./Core/library/sd_diskio_spi.d ./Core/library/sd_diskio_spi.o ./Core/library/sd_diskio_spi.su ./Core/library/sd_functions.cyclo ./Core/library/sd_functions.d ./Core/library/sd_functions.o ./Core/library/sd_functions.su ./Core/library/sd_spi.cyclo ./Core/library/sd_spi.d ./Core/library/sd_spi.o ./Core/library/sd_spi.su

.PHONY: clean-Core-2f-library


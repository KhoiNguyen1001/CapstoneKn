################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/library/INA226.c \
../Core/library/ads1115.c 

OBJS += \
./Core/library/INA226.o \
./Core/library/ads1115.o 

C_DEPS += \
./Core/library/INA226.d \
./Core/library/ads1115.d 


# Each subdirectory must supply rules for building sources it contributes
Core/library/%.o Core/library/%.su Core/library/%.cyclo: ../Core/library/%.c Core/library/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32L476xx -c -I../FATFS/Target -I../FATFS/App -I../Core/Inc -I../Drivers/STM32L4xx_HAL_Driver/Inc -I../Drivers/STM32L4xx_HAL_Driver/Inc/Legacy -I../Middlewares/Third_Party/FatFs/src -I../Drivers/CMSIS/Device/ST/STM32L4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-library

clean-Core-2f-library:
	-$(RM) ./Core/library/INA226.cyclo ./Core/library/INA226.d ./Core/library/INA226.o ./Core/library/INA226.su ./Core/library/ads1115.cyclo ./Core/library/ads1115.d ./Core/library/ads1115.o ./Core/library/ads1115.su

.PHONY: clean-Core-2f-library


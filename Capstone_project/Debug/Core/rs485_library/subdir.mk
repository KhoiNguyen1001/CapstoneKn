################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/rs485_library/epever_mppt.c \
../Core/rs485_library/modbus_rtu.c \
../Core/rs485_library/rs485.c 

OBJS += \
./Core/rs485_library/epever_mppt.o \
./Core/rs485_library/modbus_rtu.o \
./Core/rs485_library/rs485.o 

C_DEPS += \
./Core/rs485_library/epever_mppt.d \
./Core/rs485_library/modbus_rtu.d \
./Core/rs485_library/rs485.d 


# Each subdirectory must supply rules for building sources it contributes
Core/rs485_library/%.o Core/rs485_library/%.su Core/rs485_library/%.cyclo: ../Core/rs485_library/%.c Core/rs485_library/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32L476xx -c -I../FATFS/Target -I../FATFS/App -I../Core/Inc -I../Drivers/STM32L4xx_HAL_Driver/Inc -I../Drivers/STM32L4xx_HAL_Driver/Inc/Legacy -I../Middlewares/Third_Party/FatFs/src -I../Drivers/CMSIS/Device/ST/STM32L4xx/Include -I../Drivers/CMSIS/Include -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-rs485_library

clean-Core-2f-rs485_library:
	-$(RM) ./Core/rs485_library/epever_mppt.cyclo ./Core/rs485_library/epever_mppt.d ./Core/rs485_library/epever_mppt.o ./Core/rs485_library/epever_mppt.su ./Core/rs485_library/modbus_rtu.cyclo ./Core/rs485_library/modbus_rtu.d ./Core/rs485_library/modbus_rtu.o ./Core/rs485_library/modbus_rtu.su ./Core/rs485_library/rs485.cyclo ./Core/rs485_library/rs485.d ./Core/rs485_library/rs485.o ./Core/rs485_library/rs485.su

.PHONY: clean-Core-2f-rs485_library


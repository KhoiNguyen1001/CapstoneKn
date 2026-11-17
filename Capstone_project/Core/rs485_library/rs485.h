/**
 * @file    rs485.h
 * @brief   RS-485 driver for a MAX3491-family transceiver on STM32 HAL:
 *          one hardware UART + two GPIO pins (DE, RE).
 *
 * Supports two wiring/usage modes, since the MAX3491 is itself a
 * full-duplex part (separate Y/Z driver pair and A/B receiver pair):
 *
 *   - RS485_MODE_HALF_DUPLEX : classic 2-wire RS-485 bus (Y/Z shorted to
 *     A/B on the PCB, or any board that needs driver/receiver turnaround).
 *     DE and RE are toggled around every transmit. The two pins are
 *     switched one at a time through the "both enabled" state (never through
 *     the "both disabled" shutdown state of MAX348x/349x parts), so for a few
 *     nanoseconds both are on at each turnaround.
 *
 *   - RS485_MODE_FULL_DUPLEX : true 4-wire wiring (separate TX and RX
 *     pairs). DE and RE are both enabled once at init and left alone;
 *     RS485_Transmit()/RS485_Receive() can be used concurrently, e.g.
 *     RS485_Transmit() from the main loop and RS485_Receive_IT() driven
 *     from an ISR at the same time.
 *
 * DE/RE pins must already be configured as GPIO_MODE_OUTPUT_PP (e.g. by
 * MX_GPIO_Init()) and the UART must already be configured (MX_USARTx_Init())
 * before calling RS485_Init(). This library only drives the pins/UART that
 * CubeMX set up; it does not touch clock or pin-mux configuration.
 */

#ifndef RS485_H
#define RS485_H

/* Pull in the right HAL header for your family. Adjust/add the branch for
 * your target if it isn't listed, or just keep the main.h fallback, which
 * is what a standard CubeMX-generated project already includes. */
#if defined(STM32F0)
#include "stm32f0xx_hal.h"
#elif defined(STM32F1)
#include "stm32f1xx_hal.h"
#elif defined(STM32F3)
#include "stm32f3xx_hal.h"
#elif defined(STM32F4)
#include "stm32f4xx_hal.h"
#elif defined(STM32F7)
#include "stm32f7xx_hal.h"
#elif defined(STM32G0)
#include "stm32g0xx_hal.h"
#elif defined(STM32G4)
#include "stm32g4xx_hal.h"
#elif defined(STM32L0)
#include "stm32l0xx_hal.h"
#elif defined(STM32L4)
#include "stm32l4xx_hal.h"
#elif defined(STM32H7)
#include "stm32h7xx_hal.h"
#else
#include "main.h"
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bus wiring / usage mode. See file header comment for details. */
typedef enum {
    RS485_MODE_HALF_DUPLEX = 0, /**< 2-wire bus, DE/RE toggled per transfer */
    RS485_MODE_FULL_DUPLEX = 1  /**< 4-wire, DE/RE enabled permanently      */
} RS485_DuplexMode_t;

/** Current transceiver direction (meaningful only in half-duplex mode). */
typedef enum {
    RS485_DIR_RECEIVE  = 0,
    RS485_DIR_TRANSMIT = 1
} RS485_Direction_t;

/**
 * @brief Driver instance. One per RS-485 port. Zero-initialize or use
 *        RS485_Init() to fill it in; don't touch the fields directly
 *        afterwards.
 */
typedef struct {
    UART_HandleTypeDef *huart;

    GPIO_TypeDef *de_port;
    uint16_t      de_pin;
    GPIO_PinState de_active_state;   /**< pin state that ENABLES the driver   */

    GPIO_TypeDef *re_port;
    uint16_t      re_pin;
    GPIO_PinState re_active_state;   /**< pin state that ENABLES the receiver */

    RS485_DuplexMode_t duplex_mode;
    volatile RS485_Direction_t direction;
    volatile uint8_t tx_busy;        /**< set while an IT/DMA transmit is in flight */
} RS485_Handle_t;

/**
 * @brief Initialize an RS-485 handle.
 *
 * @param h                Handle to initialize.
 * @param huart             Already-configured UART handle (e.g. &huart1).
 * @param duplex_mode       RS485_MODE_HALF_DUPLEX or RS485_MODE_FULL_DUPLEX.
 * @param de_port/de_pin    GPIO driving the transceiver's DE pin.
 * @param de_active_state   GPIO_PIN_SET if DE is active-high (standard
 *                          MAX3491 wiring), GPIO_PIN_RESET if inverted.
 * @param re_port/re_pin    GPIO driving the transceiver's RE (RE-bar) pin.
 * @param re_active_state   GPIO_PIN_RESET if RE is active-low / RE-bar
 *                          (standard MAX3491 wiring), GPIO_PIN_SET if your
 *                          board inverts it.
 *
 * In half-duplex mode the handle starts in receive mode. In full-duplex
 * mode both driver and receiver are enabled immediately and stay enabled.
 */
HAL_StatusTypeDef RS485_Init(RS485_Handle_t *h,
                              UART_HandleTypeDef *huart,
                              RS485_DuplexMode_t duplex_mode,
                              GPIO_TypeDef *de_port, uint16_t de_pin, GPIO_PinState de_active_state,
                              GPIO_TypeDef *re_port, uint16_t re_pin, GPIO_PinState re_active_state);

/**
 * @brief Force the transceiver into transmit mode (driver on, receiver
 *        off). No-op in full-duplex mode, where both stay on permanently.
 *        You normally don't need to call this directly -- RS485_Transmit()
 *        and RS485_Transmit_IT() do it for you in half-duplex mode.
 */
void RS485_SetTransmitMode(RS485_Handle_t *h);

/**
 * @brief Force the transceiver into receive mode (driver off, receiver
 *        on). No-op in full-duplex mode.
 */
void RS485_SetReceiveMode(RS485_Handle_t *h);

/**
 * @brief Blocking transmit. In half-duplex mode: switches to transmit
 *        mode, sends the buffer via HAL_UART_Transmit() (which blocks
 *        until the last byte's transmission-complete flag is set, not
 *        just buffer-empty -- safe to de-assert DE right after it
 *        returns), then switches back to receive mode. In full-duplex
 *        mode, direction is left untouched.
 *
 * @return HAL status. On HAL_TIMEOUT/HAL_ERROR the transceiver is still
 *         switched back to receive mode (half-duplex) before returning,
 *         so a failed send doesn't leave the bus permanently driven.
 */
HAL_StatusTypeDef RS485_Transmit(RS485_Handle_t *h, const uint8_t *data, uint16_t size, uint32_t timeout_ms);

/**
 * @brief Blocking receive. In half-duplex mode, ensures receive mode
 *        first. Simple wrapper around HAL_UART_Receive().
 */
HAL_StatusTypeDef RS485_Receive(RS485_Handle_t *h, uint8_t *data, uint16_t size, uint32_t timeout_ms);

/**
 * @brief Interrupt-driven transmit (non-blocking). In half-duplex mode,
 *        switches to transmit mode and starts HAL_UART_Transmit_IT().
 *        You MUST call RS485_TxCpltCallback(h) from your project's
 *        HAL_UART_TxCpltCallback() (matching huart->Instance to the right
 *        handle) so the driver can switch back to receive mode at the
 *        right time -- doing it any other way risks cutting off the last
 *        byte.
 */
HAL_StatusTypeDef RS485_Transmit_IT(RS485_Handle_t *h, const uint8_t *data, uint16_t size);

/**
 * @brief Interrupt-driven receive (non-blocking). Thin wrapper around
 *        HAL_UART_Receive_IT(). In half-duplex mode, ensures receive mode
 *        first.
 */
HAL_StatusTypeDef RS485_Receive_IT(RS485_Handle_t *h, uint8_t *data, uint16_t size);

/**
 * @brief Clear UART overrun/framing/noise flags and discard any bytes waiting
 *        in the receiver. Polling receive (HAL_UART_Receive) does not check
 *        or clear these flags, so a single junk byte or overrun can leave
 *        reception stuck returning HAL_TIMEOUT until they are cleared. Called
 *        automatically before every Modbus request; call it yourself after
 *        any receive error or when bringing up the link.
 */
void RS485_FlushRx(RS485_Handle_t *h);

/**
 * @brief Call this from your global HAL_UART_TxCpltCallback(), after
 *        checking huart->Instance matches this handle's UART, whenever
 *        you use RS485_Transmit_IT(). Switches back to receive mode
 *        (half-duplex only) and clears the busy flag.
 */
void RS485_TxCpltCallback(RS485_Handle_t *h);

/**
 * @brief Returns nonzero while an RS485_Transmit_IT() transfer is still
 *        in flight.
 */
uint8_t RS485_IsTxBusy(const RS485_Handle_t *h);

#ifdef __cplusplus
}
#endif

#endif /* RS485_H */
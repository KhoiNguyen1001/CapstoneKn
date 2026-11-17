/**
 * @file rs485.c
 * @brief See rs485.h for full documentation.
 */

#include "rs485.h"

/* --- internal pin helpers ------------------------------------------- */

static inline void rs485_driver_enable(RS485_Handle_t *h)
{
    HAL_GPIO_WritePin(h->de_port, h->de_pin, h->de_active_state);
}

static inline void rs485_driver_disable(RS485_Handle_t *h)
{
    GPIO_PinState inactive = (h->de_active_state == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(h->de_port, h->de_pin, inactive);
}

static inline void rs485_receiver_enable(RS485_Handle_t *h)
{
    HAL_GPIO_WritePin(h->re_port, h->re_pin, h->re_active_state);
}

static inline void rs485_receiver_disable(RS485_Handle_t *h)
{
    GPIO_PinState inactive = (h->re_active_state == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(h->re_port, h->re_pin, inactive);
}

/* --- public API -------------------------------------------------------- */

HAL_StatusTypeDef RS485_Init(RS485_Handle_t *h,
                              UART_HandleTypeDef *huart,
                              RS485_DuplexMode_t duplex_mode,
                              GPIO_TypeDef *de_port, uint16_t de_pin, GPIO_PinState de_active_state,
                              GPIO_TypeDef *re_port, uint16_t re_pin, GPIO_PinState re_active_state)
{
    if (h == NULL || huart == NULL || de_port == NULL || re_port == NULL) {
        return HAL_ERROR;
    }

    h->huart = huart;
    h->de_port = de_port;
    h->de_pin = de_pin;
    h->de_active_state = de_active_state;
    h->re_port = re_port;
    h->re_pin = re_pin;
    h->re_active_state = re_active_state;
    h->duplex_mode = duplex_mode;
    h->tx_busy = 0;

    if (duplex_mode == RS485_MODE_FULL_DUPLEX) {
        /* True 4-wire wiring: driver and receiver can both be on at once. */
        rs485_driver_enable(h);
        rs485_receiver_enable(h);
        h->direction = RS485_DIR_TRANSMIT; /* not meaningful in this mode */
    } else {
        RS485_SetReceiveMode(h);
    }

    return HAL_OK;
}

/* Pin ordering matters on MAX3485/3491-family parts: RE high with DE low puts
 * the chip into shutdown (guaranteed NOT to shut down only if that state lasts
 * under 80 ns). Moving between "receive" (DE off, RE on) and "transmit" (DE on,
 * RE off) one pin at a time therefore has to pass through the "both enabled"
 * state, never through "both disabled". Both-enabled is harmless here: the
 * UART's TX line idles high, so the driver just holds the bus in its idle
 * state while the receiver is still listening. */
void RS485_SetTransmitMode(RS485_Handle_t *h)
{
    if (h->duplex_mode == RS485_MODE_FULL_DUPLEX) {
        return; /* both already enabled permanently */
    }
    rs485_driver_enable(h);     /* driver on first (receiver still on)...  */
    rs485_receiver_disable(h);  /* ...then silence the receiver            */
    h->direction = RS485_DIR_TRANSMIT;
}

void RS485_SetReceiveMode(RS485_Handle_t *h)
{
    if (h->duplex_mode == RS485_MODE_FULL_DUPLEX) {
        return;
    }
    rs485_receiver_enable(h);   /* receiver on first (driver still on, idle)... */
    rs485_driver_disable(h);    /* ...then release the bus                      */
    h->direction = RS485_DIR_RECEIVE;
}

HAL_StatusTypeDef RS485_Transmit(RS485_Handle_t *h, const uint8_t *data, uint16_t size, uint32_t timeout_ms)
{
    RS485_SetTransmitMode(h);

    /* HAL_UART_Transmit() in polling mode waits for the Transmission
     * Complete (TC) flag after the last byte, not just TXE, so it's safe
     * to switch the driver off as soon as this returns -- the byte is
     * fully on the wire, not still sitting in the shift register. */
    HAL_StatusTypeDef status = HAL_UART_Transmit(h->huart, (uint8_t *)data, size, timeout_ms);

    RS485_SetReceiveMode(h);
    return status;
}

HAL_StatusTypeDef RS485_Receive(RS485_Handle_t *h, uint8_t *data, uint16_t size, uint32_t timeout_ms)
{
    RS485_SetReceiveMode(h);
    return HAL_UART_Receive(h->huart, data, size, timeout_ms);
}

HAL_StatusTypeDef RS485_Transmit_IT(RS485_Handle_t *h, const uint8_t *data, uint16_t size)
{
    RS485_SetTransmitMode(h);
    h->tx_busy = 1;

    HAL_StatusTypeDef status = HAL_UART_Transmit_IT(h->huart, (uint8_t *)data, size);
    if (status != HAL_OK) {
        /* Failed to even start -- don't leave the driver latched on. */
        h->tx_busy = 0;
        RS485_SetReceiveMode(h);
    }
    return status;
}

HAL_StatusTypeDef RS485_Receive_IT(RS485_Handle_t *h, uint8_t *data, uint16_t size)
{
    RS485_SetReceiveMode(h);
    return HAL_UART_Receive_IT(h->huart, data, size);
}

void RS485_FlushRx(RS485_Handle_t *h)
{
    uint8_t junk;

    __HAL_UART_CLEAR_OREFLAG(h->huart);
    __HAL_UART_CLEAR_FEFLAG(h->huart);
    __HAL_UART_CLEAR_NEFLAG(h->huart);

    /* Zero-timeout receive returns HAL_TIMEOUT at once when nothing is
     * waiting, so this loop ends as soon as the receiver is empty. */
    for (uint8_t i = 0; i < 64; i++) {
        if (HAL_UART_Receive(h->huart, &junk, 1, 0) != HAL_OK) {
            break;
        }
    }

    __HAL_UART_CLEAR_OREFLAG(h->huart);
    __HAL_UART_CLEAR_FEFLAG(h->huart);
    __HAL_UART_CLEAR_NEFLAG(h->huart);
}

void RS485_TxCpltCallback(RS485_Handle_t *h)
{
    /* HAL_UART_TxCpltCallback() only fires once the peripheral's TC flag
     * is set (the HAL ISR waits for it), so it's safe to switch modes
     * here without corrupting the last byte. */
    h->tx_busy = 0;
    RS485_SetReceiveMode(h);
}

uint8_t RS485_IsTxBusy(const RS485_Handle_t *h)
{
    return h->tx_busy;
}
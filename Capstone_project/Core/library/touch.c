#include "touch.h"

#define I2C_TIMEOUT_MS   20

/* GT911 registers */
#define GT911_REG_ID     0x8140   /* product ID, ASCII "911" */
#define GT911_REG_CFG_XY 0x8048   /* X max (lo,hi), Y max (lo,hi) */
#define GT911_REG_STATUS 0x814E   /* bit7 = data ready, bits3:0 = point count */
#define GT911_REG_POINTS 0x814F   /* 8 bytes per point */

/* FT5x registers */
#define FT_REG_TD_STATUS 0x02     /* touch count, then 6 bytes per point */

/* ---------- low level I2C ---------- */

static HAL_StatusTypeDef reg16_read(Touch_Handle_t *t, uint16_t reg, uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Read(t->hi2c, t->addr7, reg,
                            I2C_MEMADD_SIZE_16BIT, buf, len, I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef reg16_write(Touch_Handle_t *t, uint16_t reg, const uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Write(t->hi2c, t->addr7, reg,
                             I2C_MEMADD_SIZE_16BIT, (uint8_t *)buf, len, I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef reg8_read(Touch_Handle_t *t, uint8_t reg, uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Read(t->hi2c, t->addr7, reg,
                            I2C_MEMADD_SIZE_8BIT, buf, len, I2C_TIMEOUT_MS);
}

/* ---------- coordinate remap ---------- */
static void remap(const Touch_Handle_t *t, uint16_t *x, uint16_t *y)
{
    uint16_t px = *x, py = *y;
    if (t->swap_xy) { uint16_t tmp = px; px = py; py = tmp; }
    if (t->invert_x && t->width  > 0) px = (uint16_t)(t->width  - 1 - px);
    if (t->invert_y && t->height > 0) py = (uint16_t)(t->height - 1 - py);
    *x = px;
    *y = py;
}

/* ---------- public API ---------- */
HAL_StatusTypeDef Touch_Init(Touch_Handle_t *t, I2C_HandleTypeDef *hi2c, uint16_t addr)
{
    t->irq_flag = 0;
    t->hi2c = hi2c;
    t->addr7 = addr;

    uint8_t id[4] = {0};
    uint8_t xy[4] = {0};
    if (reg16_read(t, GT911_REG_ID, id, 4) != HAL_OK) return HAL_ERROR;
    if (id[0] != '9' || id[1] != '1' || id[2] != '1') return HAL_ERROR;

    if (reg16_read(t, GT911_REG_CFG_XY, xy, 4) == HAL_OK) {
        t->raw_w = (uint16_t)(xy[0] | (xy[1] << 8));
        t->raw_h = (uint16_t)(xy[2] | (xy[3] << 8));
    }
    /* clear any stale status */
    uint8_t zero = 0;
    reg16_write(t, GT911_REG_STATUS, &zero, 1);

    return HAL_OK;
}

static int gt911_read(Touch_Handle_t *t, Touch_Point_t *pts, uint8_t max)
{
    uint8_t flag;
    uint8_t zero = 0;
    uint8_t raw[TOUCH_MAX_POINTS * 8];

    if (reg16_read(t, GT911_REG_STATUS, &flag, 1) != HAL_OK) return -1;
    if (!(flag & 0x80)) return 0;                 /* no new data */

    uint8_t n = flag & 0x0F;
    if (n > TOUCH_MAX_POINTS) n = 0;              /* invalid, just clear */
    if (n > max) n = max;

    if (n) {
        if (reg16_read(t, GT911_REG_POINTS, raw, (uint16_t)(n * 8)) != HAL_OK) return -1;
    }

    /* Must clear the status byte or the chip stops updating */
    if (reg16_write(t, GT911_REG_STATUS, &zero, 1) != HAL_OK) return -1;

    for (uint8_t i = 0; i < n; i++) {
        const uint8_t *p = &raw[i * 8];
        pts[i].id   = p[0];
        pts[i].x    = (uint16_t)(p[1] | (p[2] << 8));
        pts[i].y    = (uint16_t)(p[3] | (p[4] << 8));
        pts[i].size = (uint16_t)(p[5] | (p[6] << 8));
        remap(t, &pts[i].x, &pts[i].y);
    }
    return n;
}

int Touch_Read(Touch_Handle_t *t, Touch_Point_t *pts, uint8_t max)
{
    if (max > TOUCH_MAX_POINTS) max = TOUCH_MAX_POINTS;
    return gt911_read(t, pts, max);
}

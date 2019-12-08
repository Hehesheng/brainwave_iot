/*
 * Copyright (c) 2006-2019, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-05-02     Hehesheng    first version
 */

#include "drv_ad5933.h"
#include <stdarg.h>

#define DBG_LEVEL DBG_LOG
#define DBG_SECTION_NAME "AD59.drv"
#include <rtdbg.h>

#define AD5933_I2C_BUS_NAME "i2c1"

#define AD5933_ASSERT(x)                         \
    if (x != RT_EOK)                             \
    {                                            \
        log_e("%s: %d", __FUNCTION__, __LINE__); \
        return x;                                \
    }

typedef struct
{
    uint8_t addr;
    uint8_t len;
} ad5933_reg_t;

static struct rt_i2c_bus_device *ad5933 = RT_NULL;

static const ad5933_reg_t _regs[] = {
    AD5933_REG_CONTROL,   AD5933_REG_BEGIN_FREQ, AD5933_REG_FREQ_ADD,
    AD5933_REG_ADD_NUM,   AD5933_REG_TIME_CYCLE, AD5933_REG_STATUS,
    AD5933_REG_TEMP_DATA, AD5933_REG_REAL_DATA,  AD5933_REG_IMG_DATA,
};

static void set_buf_as_reg(uint8_t *buf, ad5933_reg_t reg, ...)
{
    va_list valist;
    va_start(valist, reg);

    for (int i = 0; i < reg.len; i++)
    {
        buf[i] = (uint8_t)va_arg(valist, int);
    }
}

static uint32_t get_freq_code(uint32_t freq)
{
    return freq * 4 * (1 << 27 / AD5933_EXTERNAL_CLOCK);
}

/* write reg */
static rt_err_t write_regs(struct rt_i2c_bus_device *bus, ad5933_reg_t reg, rt_uint8_t *data)
{
    struct rt_i2c_msg msgs;
    uint8_t write_buff[2] = {0};

    msgs.addr  = AD5933_ADDR;
    msgs.flags = RT_I2C_WR;
    msgs.buf   = write_buff;
    msgs.len   = 2;

    for (int i = 0; i < reg.len; i++)
    {
        write_buff[0] = reg.addr + i;
        write_buff[1] = data[i];

        if (rt_i2c_transfer(bus, &msgs, 1) != 1)
        {
            log_e("write faill: reg: %x", reg.addr);
            return -RT_ERROR;
        }
    }

    return RT_EOK;
}

/* 5933_write <0-8,regs><offset><data> */
static void try_5933_write(int argc, char **argv)
{
    if (argc >= 4)
    {
        struct rt_i2c_msg msgs;
        uint8_t write_buff[2] = {0};

        msgs.addr     = AD5933_ADDR;
        msgs.flags    = RT_I2C_WR;
        msgs.buf      = write_buff;
        msgs.len      = 2;
        write_buff[0] = _regs[atoi(argv[1])].addr + atoi(argv[2]);
        write_buff[1] = atoi(argv[3]);

        if (rt_i2c_transfer(ad5933, &msgs, 1) != 1)
        {
            log_e("write faill: reg: %x", _regs[atoi(argv[1])].addr + atoi(argv[2]));
        }
    }
    else
    {
        log_w("error cmd argvs");
    }
}
MSH_CMD_EXPORT_ALIAS(try_5933_write, 5933_write, 5933_write < 0 - 8regs > <offset><data>);

/* read reg */
static rt_err_t read_regs(struct rt_i2c_bus_device *bus, ad5933_reg_t reg, rt_uint8_t *data)
{
    uint8_t read_cmd[2] = {AD5933_CMD_ADDR_POINT, 0x00};
    struct rt_i2c_msg msgs[2];

    /* set pointer */
    msgs[0].addr  = AD5933_ADDR;
    msgs[0].flags = RT_I2C_WR;
    msgs[0].buf   = read_cmd;
    msgs[0].len   = 2;

    /* get data */
    msgs[1].addr  = AD5933_ADDR;
    msgs[1].flags = RT_I2C_RD;
    msgs[1].len   = 1;

    for (int i = 0; i < reg.len; i++)
    {
        read_cmd[1] = reg.addr + i;
        msgs[1].buf = data + i;
        if (rt_i2c_transfer(ad5933, msgs, 2) != 2)
        {
            log_e("read faill: reg: %x", reg.addr);
            return -RT_ERROR;
        }
    }

    return RT_EOK;
}

/* 5933_read <0-8regs><offset> */
static void try_5933_read(int argc, char **argv)
{
    if (argc >= 3)
    {
        uint8_t read_cmd[2] = {AD5933_CMD_ADDR_POINT, 0x00};
        struct rt_i2c_msg msgs[2];
        uint8_t data = 0;

        /* set pointer */
        msgs[0].addr  = AD5933_ADDR;
        msgs[0].flags = RT_I2C_WR;
        msgs[0].buf   = read_cmd;
        msgs[0].len   = 2;

        /* get data */
        msgs[1].addr  = AD5933_ADDR;
        msgs[1].flags = RT_I2C_RD;
        msgs[1].len   = 1;

        read_cmd[1] = _regs[atoi(argv[1])].addr + atoi(argv[2]);
        msgs[1].buf = &data;
        if (rt_i2c_transfer(ad5933, msgs, 2) != 2)
        {
            log_e("read faill: reg: %x", _regs[atoi(argv[1])].addr + atoi(argv[2]));
        }
        rt_kprintf("value: %x\n", data);
    }
    else
    {
        log_w("error cmd argvs");
    }
}
MSH_CMD_EXPORT_ALIAS(try_5933_read, 5933_read, 5933_read < 0 - 8regs > <offset>);

static rt_err_t ad5933_set_scan_arange(uint32_t begin, uint32_t end, uint16_t point_num)
{
    uint8_t value[4] = {0};
    uint32_t temp    = 0;

    if (begin > end || point_num > 511 || end > 500000)
    {
        return -RT_ERROR;
    }
    /* set begin freq regs */
    temp = get_freq_code(begin);
    set_buf_as_reg(value, _regs[1], (uint8_t)(temp >> 16), (uint8_t)(temp >> 8),
                   (uint8_t)(temp >> 0));
    write_regs(ad5933, _regs[1], value);

    /* set freq add num */
    temp = get_freq_code((end - begin) / point_num);
    set_buf_as_reg(value, _regs[2], (uint8_t)(temp >> 16), (uint8_t)(temp >> 8),
                   (uint8_t)(temp >> 0));
    write_regs(ad5933, _regs[2], value);

    /* set point num */
    point_num--;
    set_buf_as_reg(value, _regs[3], (uint8_t)(point_num >> 8), (uint8_t)(point_num >> 0));
    write_regs(ad5933, _regs[3], value);

    /* set time cycle = 2 */
    set_buf_as_reg(value, _regs[4], 0x00, 0x02);
    write_regs(ad5933, _regs[4], value);

    return RT_EOK;
}

static rt_err_t ad5933_scan_init(void)
{
    uint8_t value[2] = {0};
    /* set control regs: PGA x1 */
    set_buf_as_reg(value, _regs[0], 0x11, 0x00);
    write_regs(ad5933, _regs[0], value);

    return RT_EOK;
}

static rt_err_t ad5933_scan_enable(void)
{
    uint8_t value[2] = {0};
    /* set control regs: PGA x1 */
    set_buf_as_reg(value, _regs[0], 0x21, 0x00);
    write_regs(ad5933, _regs[0], value);

    return RT_EOK;
}

rt_err_t ad5933_scan_next(void)
{
    uint8_t value[2] = {0};
    /* set control regs: PGA x1 */
    set_buf_as_reg(value, _regs[0], 0x31, 0x00);
    write_regs(ad5933, _regs[0], value);

    return RT_EOK;
}

uint8_t ad5933_get_status(void)
{
    uint8_t temp = 0;
    read_regs(ad5933, _regs[5], &temp);

    return temp;
}

rt_err_t ad5933_get_fft_res(int16_t *real, int16_t *img, uint32_t timeout)
{
    rt_err_t err = RT_EOK;

    err |= read_regs(ad5933, _regs[7], (uint8_t *)real);
    err |= read_regs(ad5933, _regs[8], (uint8_t *)img);

    return err;
}

rt_err_t ad5933_start(uint32_t begin, uint32_t end, uint16_t point_num)
{
    uint8_t value[2] = {0};
    rt_err_t ret;

    /* set range */
    ret = ad5933_set_scan_arange(begin, end, point_num);
    AD5933_ASSERT(ret);
    /* set to sleep */
    set_buf_as_reg(value, _regs[0], 0xB1, 0x00);
    ret = write_regs(ad5933, _regs[0], value);
    AD5933_ASSERT(ret);

    ret = ad5933_scan_init();
    AD5933_ASSERT(ret);

    rt_thread_mdelay(200);
    ret = ad5933_scan_enable();
    AD5933_ASSERT(ret);

    return RT_EOK;
}

static int ad5933_init(void)
{
    uint8_t value[2] = {0};

    /* find ad5933 i2c bus */
    ad5933 = (struct rt_i2c_bus_device *)rt_device_find(AD5933_I2C_BUS_NAME);
    rt_device_open((rt_device_t)ad5933, RT_DEVICE_OFLAG_RDWR);

    /* Reset */
    set_buf_as_reg(value, _regs[0], 0x00, 0x10);
    write_regs(ad5933, _regs[0], value);

    log_i("ad5933 init complete");

    return 0;
}
INIT_COMPONENT_EXPORT(ad5933_init);

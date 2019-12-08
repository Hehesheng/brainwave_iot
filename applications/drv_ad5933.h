/*
 * Copyright (c) 2018-2019, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-05-02     Hehesheng    first version
 */
#ifndef __DRV_AD5933_H_ 
#define __DRV_AD5933_H_ 

#include <board.h>
#include <rtdevice.h>
#include <rtthread.h>

/* external clock 16 MHz */
#define AD5933_EXTERNAL_CLOCK (16000000)

#define AD5933_ADDR (0x0D)

#define AD5933_CMD_WRITE_BLOCK (0xA0)
#define AD5933_CMD_READ_BLOCK (0xA1)
#define AD5933_CMD_ADDR_POINT (0xB0)

#define AD5933_REG_CONTROL {.addr = 0x80, .len = 2,}
#define AD5933_REG_BEGIN_FREQ {.addr = 0x82, .len = 3,}
#define AD5933_REG_FREQ_ADD {.addr = 0x85, .len = 3,}
#define AD5933_REG_ADD_NUM {.addr = 0x88, .len = 2,}
#define AD5933_REG_TIME_CYCLE {.addr = 0x8A, .len = 2,}
#define AD5933_REG_STATUS {.addr = 0x8F, .len = 1,}
#define AD5933_REG_TEMP_DATA {.addr = 0x92, .len = 2,}
#define AD5933_REG_REAL_DATA {.addr = 0x94, .len = 2,}
#define AD5933_REG_IMG_DATA {.addr = 0x96, .len = 2,}

#define AD5933_STATUS_DATA_OK (1 << 1)
#define AD5933_STATUS_SCAN_OK (1 << 2)

rt_err_t ad5933_start(uint32_t begin, uint32_t end, uint16_t point_num);
uint8_t ad5933_get_status(void);
rt_err_t ad5933_get_fft_res(int16_t *real, int16_t *img, uint32_t timeout);
rt_err_t ad5933_scan_next(void);

#endif	// __DRV_AD5933_H_

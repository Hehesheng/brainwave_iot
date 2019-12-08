/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-11-06     SummerGift   first version
 * 2018-11-19     flybreak     add stm32f429-fire-challenger bsp
 */

/*
#define ONENET_INFO_DEVID "528233832"
#define ONENET_INFO_AUTH "tgam"
#define ONENET_INFO_APIKEY "k=0=wkvq=gqPAJL8apDMjU=8l8o="

#define ONENET_INFO_DEVID "541342082"
#define ONENET_INFO_AUTH "tgam2"
#define ONENET_INFO_APIKEY "2g4FG7C=feNvz=pwhicfDz0m8OQ=" 
 */

#include <board.h>
#include <rtdevice.h>
#include <rtthread.h>

#include <dfs_elm.h>
#include <dfs_fs.h>

#define LOG_TAG "thr.main"
#include <drv_log.h>

/* defined the LED0 pin: PD12 */
/* defined the LED1 pin: PG7 */
#define LED0_PIN GET_PIN(D, 12)
#define LED1_PIN GET_PIN(G, 7)

#define CCM_RAM_START (0x10000000)
#define CCM_RAM_SIZE (64 * 1024)

#define CPU_USAGE_CALC_TICK (10)
#define CPU_USAGE_LOOP (100)

static struct rt_memheap _ccm;

static rt_uint8_t cpu_usage_major = 0, cpu_usage_minor = 0;
static rt_uint32_t total_count = 0, total_count_times = 0;
static rt_uint32_t *cpu_buff = 0;

static void cpu_usage_idle_hook(void)
{
    rt_tick_t tick;
    rt_uint32_t count;
    volatile rt_uint32_t loop;

    if (total_count == 0)
    {
        /* get total count */
        rt_enter_critical();
        tick = rt_tick_get();
        while (rt_tick_get() - tick < CPU_USAGE_CALC_TICK)
        {
            total_count++;
            loop = 0;

            while (loop < CPU_USAGE_LOOP) loop++;
        }
        rt_exit_critical();
    }

    count = 0;
    /* get CPU usage */
    tick = rt_tick_get();
    while (rt_tick_get() - tick < CPU_USAGE_CALC_TICK)
    {
        count++;
        loop = 0;
        while (loop < CPU_USAGE_LOOP) loop++;
    }

    /* calculate major and minor */
    if (count < total_count)
    {
        count           = total_count - count;
        cpu_usage_major = (count * 100) / total_count;
        cpu_usage_minor = ((count * 100) % total_count) * 100 / total_count;
    }
    else
    {
        total_count = count;

        /* no CPU usage */
        cpu_usage_major = 0;
        cpu_usage_minor = 0;
    }
    total_count_times++;
    cpu_buff[cpu_usage_major / 10]++;
}

static void cpu_usage_init(void)
{
    /* set idle thread hook */
    cpu_buff = rt_malloc(sizeof(rt_uint32_t) * 10);
    rt_memset(cpu_buff, 0, sizeof(rt_uint32_t) * 10);
    rt_thread_idle_sethook(cpu_usage_idle_hook);
}

static void cpu(void)
{
    uint8_t len = 0;

    rt_kprintf("Total Time: %d\n", total_count_times);
    for (int i = 0; i < 10; i++)
    {
        rt_kprintf("%2d:[", i * 10);
        len = cpu_buff[i] * 10 / total_count_times;
        for (int j = 0; j < 10; j++)
        {
            if (j < len)
                rt_kprintf("-");
            else
                rt_kprintf(" ");
        }
        rt_kprintf("]%2d\n", cpu_buff[i] * 100 / total_count_times);
    }
}
MSH_CMD_EXPORT(cpu, print cpu usage);

int main(void)
{
    /* set LED0 pin mode to output */
    rt_pin_mode(LED0_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(LED1_PIN, PIN_MODE_OUTPUT);

#ifdef BSP_USING_SPI3
    if (rt_device_find("W25Q64") != RT_NULL)
    {
        if (rt_device_find("sd0") != RT_NULL)
        {
            rt_thread_mdelay(RT_TICK_PER_SECOND);
            if (dfs_mount("W25Q64", "/flash", "elm", 0, 0) == RT_EOK)
            {
                LOG_I("flash mount to '/flash'");
            }
            else
            {
                LOG_W("flash mount to '/flash' failed!");
            }
        }
        else
        {
            if (dfs_mount("W25Q64", "/", "elm", 0, 0) == RT_EOK)
            {
                LOG_I("flash mount to '/'");
            }
            else
            {
                LOG_W("flash mount to '/' failed!");
            }
        }
    }
#endif

    // cpu_usage_init();

    while (1)
    {
        rt_pin_write(LED0_PIN, PIN_HIGH);
        rt_pin_write(LED1_PIN, PIN_LOW);
        rt_thread_mdelay(500);
        rt_pin_write(LED0_PIN, PIN_LOW);
        rt_pin_write(LED1_PIN, PIN_HIGH);
        rt_thread_mdelay(500);
    }

    return RT_EOK;
}

/* init ccm ram */
static int ccm_ram_init(void)
{
    rt_memset((void *)CCM_RAM_START, 0, CCM_RAM_SIZE);

    if (rt_memheap_init(&_ccm, "ccm", (void *)CCM_RAM_START, CCM_RAM_SIZE) != RT_EOK)
    {
        rt_kprintf("ccm ram init fail\n");
        return RT_ERROR;
    }

    return RT_EOK;
}
INIT_BOARD_EXPORT(ccm_ram_init);

/*
 * Copyright (c) 2006-2019, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-05-17     Hehesheng    first version
 */

/*
RAW DATA:
    AA AA 04 80 02  数据头
    07 FF           RawData
    77              校验
BIG PACK:
    AA AA 20 02 数据头
    37          信号值
    83 18       数据头
    12 59 E5    Detal
    08 61 15    Theta
    02 70 6E    LowAlpha
    08 4F B1    HighAlpha
    00 C9 89    LowBeta
    02 DA F7    HighBeta
    01 04 F5    LowGamma
    00 FD E8    MiddleGamma
    04 00       专注度
    05 00       放松度
    68          校验
 */

#include "tgam.h"
#include "cJSON.h"

#include "hmi.h"

#define LOG_TAG "TGAM"  //该模块对应的标签。不定义时，默认：NO_TAG
#define LOG_LVL LOG_LVL_DBG  //该模块对应的日志输出级别。不定义时，默认：调试级别
#include <ulog.h>            //必须在 LOG_TAG 与 LOG_LVL 下面

#define TGAM_THREAD_STACK_SIZE (4096)
#define TGAM_THREAD_PRIORITY (15)

#ifdef TGAM_USING_DMA
#define RX_BUFF_SIZE (RT_SERIAL_RB_BUFSZ)
#define RAW_DATA_MAX_SIZE (2048)
#define GET_RAW_SIZE (5)
#define GET_PACK_SIZE (33)

static char rx_buff[RX_BUFF_SIZE + 1]   = {0};
static char data_buff[RX_BUFF_SIZE + 1] = {0};

enum
{
    STATUS_NONE,
    STATUS_AA_FIRST,
    STATUS_AA_SECOND,
    STATUS_RAW_HEAD1,
    STATUS_RAW_OK,
    STATUS_PACK_HEAD1,
    STATUS_PACK_OK,
};
#endif /* TGAM_USING_DMA */

static rt_mailbox_t tgam_mb   = RT_NULL;
static rt_mailbox_t upload_mb = RT_NULL;
static rt_device_t serial     = RT_NULL;
static uint16_t *raw_data     = RT_NULL;

static tgam_pack *pack     = RT_NULL;
static tgam_raw *raw       = RT_NULL;
static tgam_upload *upload = RT_NULL;

/**
 * @brief  装载TGAM包
 * @param  None
 * @return -1: 特殊非法字符; 0: raw数据; 1: pack数据
 */
static int tgam_msg_dump(uint8_t *buf, tgam_raw *in_raw, tgam_pack *in_pack)
{
    /* RAW dump */
    if (buf[0] == 0x80)
    {
        uint16_t tmp = 0;

        if (in_raw->len >= RAW_DATA_MAX_SIZE)
        {
            log_w("one pack data too long.");
            return 0;
        }

        tmp = (buf[2] << 8 | buf[3]);

        in_raw->raw[in_raw->len++] = tmp;

        return 0;
    }
    /* PACK dump */
    else if (buf[0] == 0x02)
    {
        uint32_t *convert_buf          = RT_NULL;
        const uint8_t _brain_wave_size = 8, _index = 4;

        convert_buf = &in_pack->detal;
        /* 填充pack */
        in_pack->sign = buf[1];
        for (int i = 0; i < _brain_wave_size; i++)
        {
            convert_buf[i] = (buf[i * 3 + _index] << 16) | (buf[i * 3 + _index + 1] << 8) |
                             (buf[i * 3 + _index + 2]);
        }
        in_pack->attention = buf[8 * 3 + _index + 1];
        in_pack->relex     = buf[8 * 3 + _index + 3];

        return 1;
    }
    else
    {
        // log_w("speical data");
        return -1;
    }
}

/**
 * @brief  将TGAM数据包装载为JSON格式
 * @param  upload: 数据
 * @return JOSN字符串
 */
static char *tgam_create_monitor(void *upload_data)
{
    tgam_upload *upload = (tgam_upload *)upload_data;
    char *string        = NULL;

    cJSON *monitor = cJSON_CreateObject();

    if (monitor == NULL)
    {
        goto end;
    }
    /* 时间轴 */
    cJSON_AddNumberToObject(monitor, "tick", upload->parent.tick);
    /* 包类型为TGAM */
    cJSON_AddStringToObject(monitor, "type", "TGAM");
    /* 原始数据 */
    {
        cJSON *arr      = NULL;
        cJSON *one_data = NULL;
        cJSON *data     = cJSON_CreateObject();

        tgam_raw *tmp = (tgam_raw *)upload->raw_data;

        cJSON_AddItemToObject(monitor, "raw_data", data);
        cJSON_AddNumberToObject(data, "len", tmp->len);
        arr = cJSON_AddArrayToObject(data, "raw");

        for (int i = 0; i < tmp->len; i++)
        {
            one_data = cJSON_CreateNumber(tmp->raw[i]);
            cJSON_AddItemToArray(arr, one_data);
        }
    }
    /* 包数据 */
    {
        tgam_pack *tmp = (tgam_pack *)upload->pack_data;
        cJSON *data    = cJSON_CreateObject();

        cJSON_AddItemToObject(monitor, "pack_data", data);

        cJSON_AddNumberToObject(data, "sign", tmp->sign);
        cJSON_AddNumberToObject(data, "detal", tmp->detal);
        cJSON_AddNumberToObject(data, "theta", tmp->theta);
        cJSON_AddNumberToObject(data, "low_alpha", tmp->low_alpha);
        cJSON_AddNumberToObject(data, "high_alpha", tmp->high_alpha);
        cJSON_AddNumberToObject(data, "low_beta", tmp->low_beta);
        cJSON_AddNumberToObject(data, "high_beta", tmp->high_beta);
        cJSON_AddNumberToObject(data, "low_gamma", tmp->low_gamma);
        cJSON_AddNumberToObject(data, "middle_gamma", tmp->middle_gamma);
        cJSON_AddNumberToObject(data, "attention", tmp->attention);
        cJSON_AddNumberToObject(data, "relex", tmp->relex);
    }

    string = cJSON_Print(monitor);
    if (string == NULL)
    {
        rt_kprintf("Failed to print monitor.\n");
    }

end:
    cJSON_Delete(monitor);
    return string;
}

/**
 * @brief  初始化tgam所需内存以及初始化
 * @param  None
 * @return None
 */
static void tgam_mem_alloc(tgam_upload **in_upload, tgam_raw **in_raw, tgam_pack **in_pack,
                           uint16_t **buf)
{
    tgam_upload *_upload = (tgam_upload *)rt_malloc(sizeof(tgam_upload));
    tgam_raw *_raw       = (tgam_raw *)rt_malloc(sizeof(tgam_raw));
    tgam_pack *_pack     = (tgam_pack *)rt_malloc(sizeof(tgam_pack));
    uint16_t *_buf       = (uint16_t *)rt_malloc(RAW_DATA_MAX_SIZE * sizeof(uint16_t));
    RT_ASSERT(((_upload) && (_raw) && (_pack) && (_buf)) != RT_NULL);
    /* 初始化资源 */
    rt_memset(_upload, 0, sizeof(tgam_upload));
    rt_memset(_raw, 0, sizeof(tgam_raw));
    rt_memset(_pack, 0, sizeof(tgam_pack));
    rt_memset(_buf, 0, RAW_DATA_MAX_SIZE * sizeof(uint16_t));
    /* 挂载 */
    _upload->raw_data  = _raw;
    _upload->pack_data = _pack;
    _raw->raw          = _buf;
    /* 输出 */
    (*in_upload) = _upload;
    (*in_raw)    = _raw;
    (*in_pack)   = _pack;
    (*buf)       = _buf;
}

static void tgam_free(void *data)
{
    tgam_upload *upload = (tgam_upload *)data;
    rt_free((void *)upload->raw_data->raw);
    rt_free((void *)upload->pack_data);
    rt_free((void *)upload->raw_data);
    rt_free((void *)upload);
}

#ifdef TGAM_USING_DMA
/**
 * @brief  串口接收的钩子函数 DMA版本
 * @param  None
 * @return None
 */
static rt_err_t serial_dma_input(rt_device_t dev, rt_size_t size)
{
    if (rt_mb_send(tgam_mb, size) != RT_EOK)
    {
        /* 丢包 */
        rt_device_read(dev, 0, RT_NULL, size);
        log_w("TGAM mailbox send fail!!!");
    }

    return RT_EOK;
}

/**
 * @brief  函数线程 DMA版本
 * @param  None
 * @return None
 */
void tgam_dma_thread(void *parameter)
{
    int len           = 0;
    int index         = 0;
    uint32_t status   = 0;
    rt_event_t events = RT_NULL;

    do
    {
        events = (rt_event_t)rt_object_find("net_event", RT_Object_Class_Event);
        rt_thread_delay(RT_TICK_PER_SECOND);
    } while (events == RT_NULL);
    rt_event_recv(events, EVENT_NET_OK, RT_EVENT_FLAG_OR, RT_WAITING_FOREVER, RT_NULL);

    /* 以读写及中断接收方式打开串口设备 */
    rt_device_open(serial, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_DMA_RX);
#ifdef TGAM_USING_DMA
    rt_device_set_rx_indicate(serial, serial_dma_input);
#else
    rt_device_set_rx_indicate(serial, serial_int_input);
#endif /* TGAM_USING_DMA */
    /* 申请上传资源 */
    tgam_mem_alloc(&upload, &raw, &pack, &raw_data);

    while (1)
    {
        /* 检测 tgam 是否在线 */
        if (rt_mb_recv(tgam_mb, (rt_ubase_t *)&len, RT_TICK_PER_SECOND * 2) != RT_EOK)
        {
            if ((events->set & EVENT_TGAM_ONLINE) != 0)
            {
                rt_event_recv(events, EVENT_TGAM_ONLINE, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, 0,
                              NULL);
                log_d("TGAM offline.");
            }
            continue;
        }
        else
        {
            if ((events->set & EVENT_TGAM_ONLINE) == 0)
            {
                rt_event_send(events, EVENT_TGAM_ONLINE);
                log_d("TGAM online.");
            }
        }

        rt_device_read(serial, 0, rx_buff, len);
        /* analysis */
        for (int i = 0; i < len; i++)
        {
            switch (status)
            {
                /* 识别数据头 */
                case STATUS_NONE:
                case STATUS_AA_FIRST:
                    if (rx_buff[i] == 0xAA)
                    {
                        status++;
                    }
                    else
                    {
                        status = STATUS_NONE;
                    }

                    break;
                case STATUS_AA_SECOND:
                    if (rx_buff[i] == 0x04)
                    {
                        status = STATUS_RAW_HEAD1;
                    }
                    else if (rx_buff[i] == 0x20)
                    {
                        status = STATUS_PACK_HEAD1;
                    }
                    else
                    {
                        status = STATUS_NONE;
                        break;
                    }
                    rt_memset(data_buff, 0, RX_BUFF_SIZE);

                    break;
                case STATUS_RAW_HEAD1:
                    data_buff[index++] = rx_buff[i];
                    /* raw 接收完成 */
                    if (index == GET_RAW_SIZE)
                    {
                        status = STATUS_RAW_OK;
                    }

                    break;
                case STATUS_PACK_HEAD1:
                    data_buff[index++] = rx_buff[i];
                    /* pack 接收完成 */
                    if (index == GET_PACK_SIZE)
                    {
                        status = STATUS_PACK_OK;
                    }

                    break;

                default:
                    break;
            }

            if (status == STATUS_RAW_OK || status == STATUS_PACK_OK)
            {
                index  = 0;
                status = STATUS_NONE;

                if (rt_event_recv(events, EVENT_UPLOAD_OK, RT_EVENT_FLAG_OR, 0, RT_NULL) != RT_EOK)
                {
                    continue;
                }
                if (tgam_msg_dump(data_buff, raw, pack) == 1)
                {
                    upload->parent.stream_name    = TGAM_ONENET_STREAM_NAME;
                    upload->parent.create_monitor = tgam_create_monitor;
                    upload->parent.free           = tgam_free;
                    upload->parent.tick           = rt_tick_get();
                    if (rt_mb_send(upload_mb, (rt_base_t)upload) != RT_EOK)
                    {
                        /* 上传失败释放内存 */
                        tgam_free(upload);
                        log_w("UPLOAD mailbox send fail!!!");
                    }
                    /* 重新申请上传资源 */
                    tgam_mem_alloc(&upload, &raw, &pack, &raw_data);
                }
            }
        }
    }
}

#else
/**
 * @brief  串口接收的钩子函数 中断版本
 * @param  None
 * @return None
 */
static rt_err_t serial_int_input(rt_device_t dev, rt_size_t size) { return RT_EOK; }

/**
 * @brief  函数线程 中断版本
 * @param  None
 * @return None
 */
void tgam_int_thread(void *parameter)
{
    uint8_t tmp = 0;

    /* 等待网络状态OK */
    while (rt_object_find("net_ready", RT_Object_Class_Semaphore) == NULL)
    {
        rt_thread_delay(RT_TICK_PER_SECOND);
    }
    /* 以读写及中断接收方式打开串口设备 */
    rt_device_open(serial, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);

    while (1)
    {
        rt_thread_delay(RT_TICK_PER_SECOND);
    }
}

#endif /* TGAM_USING_DMA */

static int tgam_component_init(void)
{
#ifdef TGAM_USING_DMA
    /* 初始化信箱 */
    tgam_mb = rt_mb_create("mTGAM", 64, RT_IPC_FLAG_FIFO);
    RT_ASSERT(tgam_mb != RT_NULL);
#endif /* TGAM_USING_DMA */

    return 0;
}
INIT_COMPONENT_EXPORT(tgam_component_init);

/**
 * @brief  初始化函数
 * @param  None
 * @return None
 */
static int tgam_app_init(void)
{
    rt_thread_t tid = RT_NULL;

    /* 打开串口 */
    serial = rt_device_find(TGAM_DEVICE_SERIAL);

    /* 设置钩子函数 */
    if (serial == RT_NULL)
    {
        log_e("serial open fail");
        return -1;
    }

    /* 获取上传给上层的信箱 */
    upload_mb = (rt_mailbox_t)rt_object_find("mUPLOAD", RT_Object_Class_MailBox);
    RT_ASSERT(upload_mb != RT_NULL);

#ifdef TGAM_USING_DMA
    tid = rt_thread_create("tTGAM", tgam_dma_thread, RT_NULL, TGAM_THREAD_STACK_SIZE,
                           TGAM_THREAD_PRIORITY, 20);
#else
    tid = rt_thread_create("tTGAM", tgam_int_thread, RT_NULL, TGAM_THREAD_STACK_SIZE,
                           TGAM_THREAD_PRIORITY, 20);
#endif /* TGAM_USING_DMA */
    // AT+LINK=884a,ea,8fb0dd
    // rt_device_write(serial, 0, "AT+LINK=884a,ea,8fb0dd\r\n",
    // rt_strlen("AT+LINK=884a,ea,8fb0dd\r\n"));
    /* 创建线程 */
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
    }
    else
    {
        log_e("TGAM thread create fail");
        return -1;
    }

    return 0;
}
INIT_APP_EXPORT(tgam_app_init);

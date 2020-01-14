#include <msh.h>

#include "app_config.h"
#include "hmi.h"

#define LOG_TAG "hmi"        //该模块对应的标签。不定义时，默认：NO_TAG
#define LOG_LVL LOG_LVL_DBG  //该模块对应的日志输出级别。不定义时，默认：调试级别
#include <ulog.h>            //必须在 LOG_TAG 与 LOG_LVL 下面

#define HMI_THREAD_STACK_SIZE (2048)
#define HMI_THREAD_PRIORITY (5)
#define RX_BUFF_SIZE (FINSH_CMD_SIZE)
#define TX_BUFF_SIZE (FINSH_CMD_SIZE)

#define HMI_CMD_END "\xFF\xFF\xFF"

static hmi_device *hmi = RT_NULL;

static void replace_string(char *str, char *delims)
{
    while (*str != '\0')
    {
        int i = 0;
        while (delims[i] != '\0')
        {
            *str = (*str == delims[i++]) ? ' ' : *str;
        }
        str++;
    }
}

static void hmi_thread(void *parameter)
{
    char ch = 0;
    int ret = 0;

    while (1)
    {
        rt_sem_take(&hmi->count, RT_WAITING_FOREVER);
        /* 读取一个字节 */
        rt_device_read(hmi->serial, 0, &ch, 1);
        if (hmi->stat == HMI_WAIT)
        {
            /* 接受开始符 */
            if (ch == 0xFE)
            {
                hmi->stat = HMI_REC;
                rt_memset((void *)hmi->rx_buff, 0, hmi->rx_len);
                hmi->rx_len = 0;
            }
            continue;
        }
        else if (hmi->stat == HMI_REC)
        {
            /* 超长命令溢出 */
            if (hmi->rx_len >= RX_BUFF_SIZE)
            {
                hmi->stat = HMI_WAIT;
                log_w("HMI Command too long.");
                continue;
            }
            /* 终止字符 */
            else if (ch == 0xFF)
            {
                hmi->rx_buff[hmi->rx_len] = '\0';
                if (hmi->rx_len == 0)
                {
                    continue;
                }
                log_d("Rec Command: %s.", hmi->rx_buff);
                /* 运行指令 */
                replace_string(hmi->rx_buff, ALLOW_SPLIT_CHARS);
                ret = msh_exec(hmi->rx_buff, hmi->rx_len);
                if (ret == -1)
                {
                    log_w("Error Command: %s.", hmi->rx_buff);
                }
                else if (ret != 0)
                {
                    log_d("HMI Result: %d", ret);
                }

                hmi->stat = HMI_WAIT;
                continue;
            }
            hmi->rx_buff[hmi->rx_len] = ch;
            hmi->rx_len++;
        }
    }
}

/**
 * @brief  串口3接受的钩子函数
 * @param  None
 * @return None
 */
static rt_err_t hmi_serial_input(rt_device_t dev, rt_size_t size)
{
    /* 串口接收到数据后产生中断，调用此回调函数，然后发送接收信号量 */
    rt_sem_release(&hmi->count);

    return RT_EOK;
}

static int hmi_create(void)
{
    rt_thread_t tid = RT_NULL;
    rt_err_t ret;

    /* 创建hmi设备类 */
    hmi = (hmi_device *)rt_malloc(sizeof(hmi_device));
    if (hmi != RT_NULL)
    {
        rt_memset((void *)hmi, 0, sizeof(hmi_device));
    }
    else
    {
        log_e("HMI device mem alloc fail.");
        return -1;
    }
    /* 打开串口 */
    hmi->serial = rt_device_find(HMI_DEVICE_SERIAL_NAME);
    if (hmi->serial != RT_NULL)
    {
        /* 以读写及中断接收方式打开串口设备 */
        rt_device_open(hmi->serial, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
        /* 设置钩子函数 */
        rt_device_set_rx_indicate(hmi->serial, hmi_serial_input);
    }
    else
    {
        log_e("HMI serial is not finded.");
        return -1;
    }
    /* 创建信号量通信 */
    ret = rt_sem_init(&hmi->count, "shmi", 0, RT_IPC_FLAG_PRIO);
    if (ret != RT_EOK)
    {
        log_e("HMI sem create fail.");
        return -1;
    }
    /* 注册设备 */
    ret = rt_device_register(&hmi->parent, "hmi", 0);
    if (ret != RT_EOK)
    {
        log_e("HMI register fail.");
        return -1;
    }
    /* 创建线程 */
    tid = rt_thread_create("thmi", hmi_thread, RT_NULL, HMI_THREAD_STACK_SIZE, HMI_THREAD_PRIORITY, 20);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
    }
    else
    {
        log_e("hmi thread create fail");
        return -1;
    }

    hmi_send("main.debug", "txt", "\"Online\"");

    return 0;
}
INIT_APP_EXPORT(hmi_create);

int hmi_send(char *name, char *attribute, char *format, ...)
{
    rt_int32_t n;
    va_list args;

    hmi->tx_len = rt_sprintf(hmi->tx_buff, "%s.%s=", name, attribute);

    va_start(args, format);
    hmi->tx_len += rt_vsnprintf(hmi->tx_buff + hmi->tx_len, TX_BUFF_SIZE - hmi->tx_len, format, args);
    va_end(args);
    rt_device_write(hmi->serial, 0, hmi->tx_buff, hmi->tx_len);
    rt_device_write(hmi->serial, 0, HMI_CMD_END, rt_strlen(HMI_CMD_END));

    return n;
}

/*
 * Copyright (c) 2006-2019, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-04-13     Hehesheng    first version
 */

/*
    wifi 文件保存格式使用 json
    example:
    {
        "wifi": [
            {
                "ssid": "hehe-free",
                "key": "threegeeks",
            },
            {
                "ssid": "dianzibu",
                "key": "castelec",
            },
        ]
    }
 */

#include <board.h>
#include <rtdevice.h>
#include <rtthread.h>

#include <wlan_cfg.h>
#include <wlan_mgnt.h>
#include <wlan_prot.h>

#include <cJSON.h>
#include <cJSON_Utils.h>
#include <dfs_posix.h>
#include <dstr.h>

#include "app_config.h"
#include "hmi.h"

#define DBG_LEVEL DBG_LOG
#define DBG_SECTION_NAME "WIFI.env"
#include <rtdbg.h>

#define WIFI_CONFIG_NAME "wifi.cfg"

static rt_event_t net_event = NULL;

static void wifi_event_callback(int event, struct rt_wlan_buff *buff, void *parameter)
{
    if (event == RT_WLAN_EVT_READY)
    {
        hmi_send("main.debug", "txt", "\"Network OK!\"");
        LOG_I("%s: Network Ready", __FUNCTION__);
        rt_event_send(net_event, EVENT_NET_OK);
    }
    else if (event == RT_WLAN_EVT_STA_CONNECTED)
    {
        hmi_send("main.debug", "txt", "\"connect to %s.\"",
                 ((struct rt_wlan_info *)buff->data)->ssid.val);
        LOG_I("%s: ssid : %s connected", __FUNCTION__,
              ((struct rt_wlan_info *)buff->data)->ssid.val);
        rt_event_send(net_event, EVENT_WLAN_OK);
    }
    else if (event == RT_WLAN_EVT_STA_DISCONNECTED)
    {
        hmi_send("main.debug", "txt", "\"wifi disconnected.\"");
        LOG_W("%s: ssid : %s disconnected", __FUNCTION__,
              ((struct rt_wlan_info *)buff->data)->ssid.val);
        rt_event_recv(net_event, EVENT_WLAN_OK | EVENT_NET_OK,
                      RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR, RT_WAITING_FOREVER, NULL);
    }
    else if (event == RT_WLAN_EVT_STA_CONNECTED_FAIL)
    {
        hmi_send("main.debug", "txt", "\"wifi connected fail.\"");
        LOG_W("%s: ssid : %s connected fail", __FUNCTION__,
              ((struct rt_wlan_info *)buff->data)->ssid.val);
    }
}

static uint32_t wifi_join(char *ssid, char *key)
{
    rt_err_t res = 0;

    rt_wlan_config_autoreconnect(RT_FALSE);
    rt_thread_delay(RT_TICK_PER_SECOND / 5);
    res = rt_wlan_connect(ssid, key);
    rt_wlan_config_autoreconnect(RT_TRUE);

    return res;
}

static uint32_t wifi_cjson_join(cJSON *json_wifi_info)
{
    int ret = -RT_ERROR;

    cJSON *ssid = NULL;
    cJSON *key  = NULL;

    ssid = cJSON_GetObjectItemCaseSensitive(json_wifi_info, "ssid");
    key  = cJSON_GetObjectItemCaseSensitive(json_wifi_info, "key");

    if (cJSON_IsString(ssid))
    {
        if (key == NULL)
        {
            ret = wifi_join(ssid->valuestring, NULL);
        }
        else if (cJSON_IsString(key))
        {
            ret = wifi_join(ssid->valuestring, key->valuestring);
        }
    }

    return ret;
}

static char *wifi_get_config_path(void)
{
    char *path = NULL;

    /* 找寻片上flash */
    if (rt_device_find("W25Q64") == NULL)
    {
        return NULL;
    }
    /* 是否挂载sd卡 */
    if (rt_device_find("sd0") != NULL)
    {
        path = "/flash/" WIFI_CONFIG_NAME;
    }
    else
    {
        path = WIFI_CONFIG_NAME;
    }

    return path;
}

static int32_t wifi_auto_connect(void)
{
    int ret = -RT_ERROR;

    rt_dstr_t *config = NULL;

    struct rt_wlan_scan_result *scan_result = NULL;
    /* 初始化等待 */
    {
        int timeout = 0;

        while (!rt_device_find(RT_WLAN_DEVICE_STA_NAME))
        {
            rt_thread_delay(RT_TICK_PER_SECOND);
            timeout++;
            if (timeout >= 10)
            {
                LOG_E("find wifi device timeout");
                return -RT_ETIMEOUT;
            }
        }
    }
    rt_thread_delay(RT_TICK_PER_SECOND);

    /* Configuring WLAN device working mode */
    rt_wlan_set_mode(RT_WLAN_DEVICE_STA_NAME, RT_WLAN_STATION);
    rt_wlan_config_autoreconnect(RT_TRUE);
    /* set event handler */
    rt_wlan_register_event_handler(RT_WLAN_EVT_READY, wifi_event_callback, NULL);
    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_CONNECTED, wifi_event_callback, NULL);
    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_DISCONNECTED, wifi_event_callback, NULL);
    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_CONNECTED_FAIL, wifi_event_callback, NULL);

    /* scan ap info */
    {
        log_d("scan wifi...");
        scan_result = rt_wlan_scan_sync();
        if (scan_result == NULL)
        {
            log_w("wifi scan fail.");
            rt_wlan_scan_result_clean();
            return -RT_ENOSYS;
        }
    }
    /* 读取 wifi 配置文件 */
    {
        int f      = 0;
        int n      = 0;
        char *path = NULL;
        char *buff = NULL;

        path = wifi_get_config_path();

        f = open(path, O_RDONLY | O_CREAT);
        if (f < 0)
        {
            log_w(WIFI_CONFIG_NAME " open error.");
            return -RT_EIO;
        }

        buff = rt_malloc(1024 + 1);
        rt_memset(buff, 0, 1024 + 1);
        config = rt_dstr_new(NULL);

        while (1)
        {
            n = read(f, buff, 1024);
            if (n)
            {
                config = rt_dstr_cat(config, buff);
            }
            if (n != 1024)
            {
                break;
            }
        }

        rt_free(buff);
        close(f);
    }
    /* 加载配置 */
    {
        cJSON *monitor   = NULL;
        cJSON *arr       = NULL;
        cJSON *json_wifi = NULL;
        cJSON *json_ssid = NULL;

        monitor = cJSON_Parse(config->str);
        rt_dstr_del(config);
        if (monitor == NULL)
        {
            log_w(WIFI_CONFIG_NAME " format error.");
            return -RT_ENOSYS;
        }
        arr = cJSON_GetObjectItemCaseSensitive(monitor, "wifi");
        if (cJSON_IsArray(arr))
        {
            cJSON_ArrayForEach(json_wifi, arr)
            {
                json_ssid = cJSON_GetObjectItemCaseSensitive(json_wifi, "ssid");
                if (cJSON_IsString(json_ssid))
                {
                    for (int index = 0; index < scan_result->num; index++)
                    {
                        if (rt_strcmp(json_ssid->valuestring, scan_result->info[index].ssid.val) ==
                            0)
                        {
                            log_d("Start to connect wifi: %s", json_ssid->valuestring);
                            ret = wifi_cjson_join(json_wifi);
                            /* ssid 一致 但是连接失败 */
                            if (ret)
                            {
                                log_w("Connected fail.");
                                continue;
                            }
                            goto cjson_end;
                        }
                    }
                }
            }
            log_w("No wifi can be connected.");
        }
        else
        {
            log_w(WIFI_CONFIG_NAME " [wifi] item type error.");
        }

    cjson_end:
        cJSON_Delete(monitor);
    }

    return ret;
}

static void wifi_connect_thread(void *param)
{
    rt_err_t result = -RT_ERROR;

    result = wifi_auto_connect();

    if (result != RT_EOK)
    {
        log_w("wifi connected fail, Error: %d", result);
        return;
    }

    log_i("wifi auto connected.");
}

static int wifi_connect_create(void)
{
    rt_thread_t tid = NULL;

    net_event = rt_event_create("net_event", RT_IPC_FLAG_FIFO);
    RT_ASSERT(net_event != NULL);

    tid = rt_thread_create("wifi_c", wifi_connect_thread, NULL, 4096, 25, 20);
    if (tid != NULL)
    {
        rt_thread_startup(tid);
    }

    return 0;
}
INIT_ENV_EXPORT(wifi_connect_create);

static void wifi_config(int argc, char **argv)
{
    int f      = 0;
    int n      = 0;
    char *path = NULL;
    char *buff = NULL;

    rt_dstr_t *config = NULL;
    cJSON *monitor    = NULL;
    cJSON *arr        = NULL;
    cJSON *wifi_info  = NULL;
    cJSON *saved      = NULL;

    path = wifi_get_config_path();

    f = open(path, O_RDONLY | O_CREAT);
    if (f < 0)
    {
        log_w(WIFI_CONFIG_NAME " open error.");
        return;
    }

    buff = rt_malloc(1024 + 1);
    rt_memset(buff, 0, 1024 + 1);
    config = rt_dstr_new(NULL);
    while (1)
    {
        n = read(f, buff, 1024);
        if (n)
        {
            config = rt_dstr_cat(config, buff);
        }
        if (n != 1024)
        {
            break;
        }
    }
    rt_free(buff);
    close(f);

    monitor = cJSON_Parse(config->str);
    rt_dstr_del(config);
    if (monitor == NULL)
    {
        /* 格式不存在 创建 */
        log_d(WIFI_CONFIG_NAME " format error, recreate.");
        monitor = cJSON_CreateObject();
    }
    arr = cJSON_GetObjectItemCaseSensitive(monitor, "wifi");
    if ((arr == NULL))
    {
        /* wifi array 不存在 创建 */
        log_d(WIFI_CONFIG_NAME " format error, recreate.");
        arr = cJSON_AddArrayToObject(monitor, "wifi");
    }
    /* 装载数据 */
    if (argc >= 3)
    {
        wifi_info = cJSON_CreateObject();
        cJSON_AddStringToObject(wifi_info, "ssid", argv[2]);
    }
    if (argc >= 4)
    {
        cJSON_AddStringToObject(wifi_info, "key", argv[3]);
    }
    /* 添加或删除 */
    if (rt_strcmp("add", argv[1]) == 0)
    {
        /* 如果有一样的直接跳过 */
        cJSON_ArrayForEach(saved, arr)
        {
            if (cJSON_Compare(wifi_info, saved, 1))
            {
                goto _exit;
            }
        }
        /* 将新的 wifi 信息加入到 json 头中 */
        cJSON_InsertItemInArray(arr, 0, wifi_info);
        /* 存储太多 删去旧的 */
        if (cJSON_GetArraySize(arr) > 10)
        {
            cJSON_DeleteItemFromArray(arr, 10);
        }
    }
    else if (rt_strcmp("remove", argv[1]) == 0)
    {
        /* 寻找一样的并删除 */
        int i = 0;
        cJSON_ArrayForEach(saved, arr)
        {
            if (cJSON_Compare(wifi_info, saved, 1))
            {
                cJSON_DeleteItemFromArray(arr, i);
                break;
            }
            i++;
        }
    }
    /* 保存 */
    buff = cJSON_Print(monitor);
    if (buff != NULL)
    {
        f = open(path, O_WRONLY | O_CREAT | O_TRUNC);
        write(f, buff, rt_strlen(buff));
        close(f);
        rt_free(buff);
    }
    else
    {
        log_w("%s", cJSON_GetErrorPtr());
    }

_exit:
    cJSON_Delete(wifi_info);
    cJSON_Delete(monitor);
}
MSH_CMD_EXPORT(wifi_config, add allowed wifi : wifi_config_add<add | remove><ssid>[key])

static int wifi_add(int argc, char **argv)
{
    char *_argv[4] = {
        "wifi_config",
        "add",
    };
    rt_err_t res = 0;

    wifi_join(argv[1], argv[2]);
    if (res != RT_EOK)
    {
        log_w("wifi connected fail, Error: %d", res);
        return -1;
    }
    /* 连接成功记忆 wifi */
    _argv[2] = argv[1];
    if (argc == 2)
    {
        wifi_config(3, _argv);
    }
    else if (argc >= 3)
    {
        _argv[3] = argv[2];
        wifi_config(4, _argv);
    }

    return 0;
}
MSH_CMD_EXPORT(wifi_add, NONE);

static int getWifiStatus(int argc, char **argv)
{
    struct rt_wlan_info info;

    if (rt_wlan_is_connected() == 1)
    {
        hmi_send("wifi_state", "val", "1");
        rt_wlan_get_info(&info);
        hmi_send("wifi_ssid", "txt", "\"%-.32s\"", &info.ssid.val[0]);
    }
    else
    {
        hmi_send("wifi_state", "val", "0");
        hmi_send("wifi_ssid", "txt", "\"NULL\"");
    }

    return 0;
}
MSH_CMD_EXPORT(getWifiStatus, NONE);

/*****************************************************************/ /**
* @file qurl_http_get_redirect_demo.c
* @brief
* @author harry.li@quectel.com
* @date 2025-08-18
*
* @copyright Copyright (c) 2023 Quectel Wireless Solution, Co., Ltd.
* All Rights Reserved. Quectel Wireless Solution Proprietary and Confidential.
*
* @par EDIT HISTORY FOR MODULE
* <table>
* <tr><th>Date <th>Version <th>Author <th>Description
* <tr><td>2025-05-7 <td>1.0 <td>harry.li <td> Init
* </table>
**********************************************************************/

#include "qosa_sys.h"
#include "qosa_def.h"
#include "qosa_log.h"
#include "qurl.h"
#include "unirtos_app_init_registry.h"

#define QOS_LOG_TAG                    LOG_TAG

#define UNIR_HTTP_DEMO_TASK_STACK_SIZE 4096
#define UNIR_HTTP_DEMP_PDPID           1

static long qurl_http_app_w_cb(char *buf, long size, void *arg)
{
    QOSA_UNUSED(arg);

    QLOGD("size=%d,%s", size, buf);
    return size;
}

/**
 * @brief Initialize HTTP redirect test application
 * @details This function is used to initialize HTTP redirect function test, configure HTTP GET request and execute redirect operation
 * @param None
 * @return qurl_ecode_t - Returns execution result status code
 *         - QURL_OK: Execution successful
 *         - Other values: Execution failed
 */
qurl_ecode_t qurl_http_app_get_redirect_init(void)
{
    qurl_ecode_t ret = QURL_OK;
    qurl_core_t  core = QOSA_NULL;

    // Initialize global qurl environment
    qurl_global_init();

    // Create qurl core handle
    ret = qurl_core_create(&core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

    // Set network ID (PDP context ID)
    qurl_core_setopt(core, QURL_OPT_NETWORK_ID, UNIR_HTTP_DEMP_PDPID);
    // Configure as HTTP GET method
    qurl_core_setopt(core, QURL_OPT_HTTP_GET, 1L);

    /* Set target URL, using httpbin.org's absolute-redirect/{n} test interface */
    // Will redirect to http://httpbin.org/get URL
    ret = qurl_core_setopt(core, QURL_OPT_URL, "http://httpbin.org/absolute-redirect/1");
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

    // Set response body callback function
    ret = qurl_core_setopt(core, QURL_OPT_WRITE_CB, qurl_http_app_w_cb);

    // Enable redirect support function
    ret = qurl_core_setopt(core, QURL_OPT_FOLLOWLOCATION, 1);

    // Execute HTTP request operation
    ret = qurl_core_perform(core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

    // Delete qurl core handle, release resources
    ret = qurl_core_delete(core);
    return QURL_OK;
}

static void qurl_http_app_redirect_init(void *argv)
{
    qosa_task_sleep_sec(10);
    qurl_http_app_get_redirect_init();
}

void unir_qurl_http_redirect_demo_init(void)
{
    int         err = 0;
    qosa_task_t http_task = QOSA_NULL;

    err = qosa_task_create(&http_task, UNIR_HTTP_DEMO_TASK_STACK_SIZE, QOSA_PRIORITY_NORMAL, "http_auth_demo", qurl_http_app_redirect_init, QOSA_NULL);
    if (err != QOSA_OK)
    {
        QLOGE("task create error");
        return;
    }
}

UNIRTOS_APP_EXPORT(362, "qurl_http_redirect_demo", unir_qurl_http_redirect_demo_init);
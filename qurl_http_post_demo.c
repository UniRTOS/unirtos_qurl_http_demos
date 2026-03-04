/*****************************************************************/ /**
* @file qurl_http_post_demo.c
* @brief
* @author harry.li@quectel.com
* @date 2025-05-7
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
#include "qosa_virtual_file.h"

#define QOS_LOG_TAG                    LOG_TAG

#define UNIR_HTTP_DEMO_TASK_STACK_SIZE 4096
#define UNIR_HTTP_DEMP_PDPID           1

#define QURL_HTTP_APP_MAX(a, b)        (((a) > (b)) ? (a) : (b))
#define QURL_HTTP_APP_MIN(a, b)        (((a) < (b)) ? (a) : (b))

struct qurl_app_r_buf_s
{
    char *buf_ptr;
    long  buf_len;
    long  r_index;
};
typedef struct qurl_app_r_buf_s qurl_app_r_buf_t;

static char g_form_data2[] = "this is test2";

static long qurl_http_app_w_h_cb(char *buf, long size, void *arg)
{
    // Prevent unused parameter warning
    QOSA_UNUSED(arg);

    // Record received data size and content to application log
    QLOGD("size=%d,%s", size, buf);
    return size;
}

/**
 * @brief HTTP application layer read form data callback function
 *
 * @param buf Buffer pointer for storing read data
 * @param size Requested read data size
 * @param arg Pointer to user-passed data
 *
 * @return Actual uploaded data size, returns 0 indicates no data to read
 */
long qurl_http_app_r_form_cb(unsigned char *buf, long size, void *arg)
{
    qurl_app_r_buf_t *r_buf_ptr = (qurl_app_r_buf_t *)arg;

    /* Check if requested write data size is valid */
    if (size < 1)
    {
        return 0;
    }

    /* Calculate actual writable data size to avoid exceeding buffer boundaries */
    size = QURL_HTTP_APP_MIN(size, r_buf_ptr->buf_len - r_buf_ptr->r_index);

    /* Copy data from buffer to target buffer */
    qosa_memcpy(buf, r_buf_ptr->buf_ptr + r_buf_ptr->r_index, size);

    /* Update write index position */
    r_buf_ptr->r_index += size;

    /* All written */
    if (r_buf_ptr->r_index >= r_buf_ptr->buf_len)
    {
        r_buf_ptr->r_index = 0;
    }

    return size;
}

static long qurl_http_app_w_cb(char *buf, long size, void *arg)
{
    QOSA_UNUSED(arg);

    QLOGD("size=%d,%s", size, buf);
    return size;
}

/**
 * @brief Execute HTTP POST form upload operation, demonstrating uploading form data through different methods.
 *
 * This function uses the qurl library to complete an HTTP POST request, uploading multiple form fields including:
 * - Directly uploading string data;
 * - Uploading data from global variables;
 * - Uploading data through callback functions.
 *
 * Also sets response header and response body callback processing functions, and prints response status code and content length after request completion.
 *
 * @return qurl_ecode_t Execution result status code, QURL_OK indicates success.
 */
qurl_ecode_t qurl_http_app_post_form(void)
{
    qurl_ecode_t         ret = QURL_OK;
    qurl_core_t          core = QOSA_NULL;
    long                 resp_code = 0;
    long                 content_length = 0;
    qurl_http_form_cfg_t form_cfg = {0};
    qurl_app_r_buf_t     r_buff = {0};

    // Initialize global resources
    qurl_global_init();

    // Create core handle
    ret = qurl_core_create(&core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

    // Set target URL
    ret = qurl_core_setopt(core, QURL_OPT_URL, "http://httpbin.org/post");
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

    // Set network ID and POST form mode
    qurl_core_setopt(core, QURL_OPT_NETWORK_ID, UNIR_HTTP_DEMP_PDPID);
    qurl_core_setopt(core, QURL_OPT_HTTP_POST_FORM, 1L);

    // Add first form field: directly upload string data
    form_cfg.name_ptr = "test1";
    form_cfg.filename_ptr = "file1";
    form_cfg.content_type = QURL_HTTP_FORM_CONTENT_DATA;
    form_cfg.content_ptr = "this is test1";
    form_cfg.read_content_func = QOSA_NULL;
    form_cfg.content_len = qosa_strlen(form_cfg.content_ptr);
    qurl_core_setopt(core, QURL_OPT_FORM, 1L, &form_cfg);

    // Add second form field: upload string data from global variables
    form_cfg.name_ptr = "test2";
    form_cfg.filename_ptr = "file2";
    form_cfg.content_type = QURL_HTTP_FORM_CONTENT_DATA;
    form_cfg.content_ptr = g_form_data2;
    form_cfg.read_content_func = QOSA_NULL;
    form_cfg.content_len = qosa_strlen(g_form_data2);
    qurl_core_setopt(core, QURL_OPT_FORM, 2L, &form_cfg);

    // Add third form field: upload data through callback function
    r_buff.buf_ptr = qosa_malloc(20);
    qosa_memset(r_buff.buf_ptr, 0, 20);
    qosa_snprintf(r_buff.buf_ptr, 19, "%s", "this is test3");
    r_buff.r_index = 0;
    r_buff.buf_len = qosa_strlen(r_buff.buf_ptr);
    form_cfg.name_ptr = "test3";
    form_cfg.filename_ptr = "file3";
    form_cfg.content_type = QURL_HTTP_FORM_CONTENT_CB;
    form_cfg.read_content_func = qurl_http_app_r_form_cb;
    form_cfg.content_ptr = &r_buff;
    form_cfg.content_len = r_buff.buf_len;

    qurl_core_setopt(core, QURL_OPT_FORM, 3L, &form_cfg);

    // Set response header and response body callback processing functions
    qurl_core_setopt(core, QURL_OPT_WRITE_HEAD_CB, qurl_http_app_w_h_cb);
    qurl_core_setopt(core, QURL_OPT_WRITE_CB, qurl_http_app_w_cb);

    // Execute HTTP request
    ret = qurl_core_perform(core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

    // Execute again (possibly for retry or debugging)
    qurl_core_perform(core);

    // Get response status code
    qurl_core_getinfo(core, QURL_INFO_RESP_CODE, &resp_code);
    QLOGV("resp_code:[%d]\r\n", resp_code);

    // Get response content length
    qurl_core_getinfo(core, QURL_INFO_RESP_CONTENT_LENGTH, &content_length);
    QLOGV("content_length:[%d]\r\n", content_length);

    // Delete core handle, release resources
    ret = qurl_core_delete(core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }
    qosa_free(r_buff.buf_ptr);

    return QURL_OK;
}

static void unir_http_post_app_init(void *argv)
{
    qurl_ecode_t ret = QURL_OK;

    qosa_task_sleep_sec(10);
    ret = qurl_http_app_post_form();
    QLOGE("ret=%d", ret);
}

void unir_qurl_http_post_demo_init(void)
{
    int         err = 0;
    qosa_task_t http_task = QOSA_NULL;

    err = qosa_task_create(&http_task, UNIR_HTTP_DEMO_TASK_STACK_SIZE, QOSA_PRIORITY_NORMAL, "http_post_demo", unir_http_post_app_init, QOSA_NULL);
    if (err != QOSA_OK)
    {
        QLOGE("task create error");
        return;
    }
}

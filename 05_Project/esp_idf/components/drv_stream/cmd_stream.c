/* *****************************************************************************
 * File:   cmd_stream.c
 * Author: Dimitar Lilov
 *
 * Created on 2022 06 18
 * 
 * Description: ...
 * 
 **************************************************************************** */

/* *****************************************************************************
 * Header Includes
 **************************************************************************** */
#include "cmd_stream.h"
#include "drv_stream.h"

#include <string.h>

#include "esp_log.h"
#include "esp_console.h"
#include "esp_system.h"

#include "argtable3/argtable3.h"

#include "drv_console_if.h"

/* *****************************************************************************
 * Configuration Definitions
 **************************************************************************** */
#define TAG "cmd_stream"

/* *****************************************************************************
 * Constants and Macros Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Enumeration Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Type Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Function-Like Macros
 **************************************************************************** */

/* *****************************************************************************
 * Variables Definitions
 **************************************************************************** */

static struct {
    struct arg_str *stream;
    struct arg_str *command;
    struct arg_end *end;
} stream_args;


/* *****************************************************************************
 * Prototype of functions definitions
 **************************************************************************** */

/* *****************************************************************************
 * Functions
 **************************************************************************** */
static int update_stream(int argc, char **argv)
{
    drv_console_set_other_log_disabled();

    ESP_LOGI(__func__, "argc=%d", argc);
    for (int i = 0; i < argc; i++)
    {
        ESP_LOGI(__func__, "argv[%d]=%s", i, argv[i]);
    }

    int nerrors = arg_parse(argc, argv, (void **)&stream_args);
    if (nerrors != ESP_OK)
    {
        arg_print_errors(stderr, stream_args.end, argv[0]);
        return ESP_FAIL;
    }


    const char* stream_name = stream_args.stream->sval[0];
    const char* stream_command = stream_args.command->sval[0];
    if (strcmp(stream_command,"list") == 0)
    {
        drv_stream_list();
    }    
    else
    if (strlen(stream_name) > 0)
    {
        int index = drv_stream_get_position(stream_name);
        if ( index >= 0 )
        {
            ESP_LOGW(TAG, "Found Stream[%d] Name %s", index, stream_name);
        }
        else
        {
            ESP_LOGE(TAG, "Error Stream %s not found", stream_name);
        }
        drv_stream_t* pStream = drv_stream_get_handle(stream_name);
        if (pStream != NULL)
        {
            if (strcmp(stream_command,"size") == 0)
            {
                int nSize = drv_stream_get_size(pStream);
                ESP_LOGW(TAG, "Stream %s Size:%d", stream_name, nSize);
            }
        }

    }


    return 0;
}

static void register_stream(void)
{
    stream_args.stream = arg_strn("s", "stream", "<stream>", 0, 1, "Command can be : stream [-s stream_name]");
    stream_args.command = arg_strn(NULL, NULL, "<command>", 1, 1, "Command can be : stream {list|size}");
    stream_args.end = arg_end(4);

    const esp_console_cmd_t cmd_stream = {
        .command = "stream",
        .help = "Stream Settings Manage Request",
        .hint = NULL,
        .func = &update_stream,
        .argtable = &stream_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_stream));
}


void cmd_stream_register(void)
{
    register_stream();
}
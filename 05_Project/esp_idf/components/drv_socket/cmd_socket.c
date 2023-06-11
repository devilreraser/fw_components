/* *****************************************************************************
 * File:   cmd_socket.c
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
#include "cmd_socket.h"
#include "drv_socket.h"

#include <string.h>

#include "esp_log.h"
#include "esp_console.h"
#include "esp_system.h"

#include "argtable3/argtable3.h"

#include "drv_console_if.h"


/* *****************************************************************************
 * Configuration Definitions
 **************************************************************************** */
#define TAG "cmd_socket"

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
    struct arg_str *socket;
    struct arg_str *command;
    struct arg_end *end;
} socket_args;


/* *****************************************************************************
 * Prototype of functions definitions
 **************************************************************************** */

/* *****************************************************************************
 * Functions
 **************************************************************************** */
static int update_socket(int argc, char **argv)
{
    drv_console_set_other_log_disabled();

    ESP_LOGI(__func__, "argc=%d", argc);
    for (int i = 0; i < argc; i++)
    {
        ESP_LOGI(__func__, "argv[%d]=%s", i, argv[i]);
    }

    int nerrors = arg_parse(argc, argv, (void **)&socket_args);
    if (nerrors != ESP_OK)
    {
        arg_print_errors(stderr, socket_args.end, argv[0]);
        return ESP_FAIL;
    }

    const char* socket_name = socket_args.socket->sval[0];
    const char* socket_command = socket_args.command->sval[0];
    if (strcmp(socket_command,"list") == 0)
    {
        drv_socket_list();
    }    
    else
    if (strlen(socket_name) > 0)
    {
        int index = drv_socket_get_position(socket_name);
        if ( index >= 0 )
        {
            ESP_LOGW(TAG, "Found Socket[%d] Name %s", index, socket_name);
        }
        else
        {
            ESP_LOGE(TAG, "Error Socket %s not found", socket_name);
        }
        drv_socket_t* pSocket = drv_socket_get_handle(socket_name);
        if (pSocket != NULL)
        {
            if (strcmp(socket_command,"stop") == 0)
            {
                drv_socket_disconnect(pSocket);
            }
        }
    }
    return 0;
}

static void register_socket(void)
{
    socket_args.socket = arg_strn("s", "socket", "<socket>", 0, 1, "Command can be : socket [-s socket_name]");
    socket_args.command = arg_strn(NULL, NULL, "<command>", 0, 1, "Command can be : socket {start|stop|list}");
    socket_args.end = arg_end(4);

    const esp_console_cmd_t cmd_socket = {
        .command = "socket",
        .help = "Socket Settings Manage Request",
        .hint = NULL,
        .func = &update_socket,
        .argtable = &socket_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_socket));
}


void cmd_socket_register(void)
{
    register_socket();
}
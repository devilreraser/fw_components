/* *****************************************************************************
 * File:   drv_stream.h
 * Author: Dimitar Lilov
 *
 * Created on 2022 06 18
 * 
 * Description: ...
 * 
 **************************************************************************** */
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */


/* *****************************************************************************
 * Header Includes
 **************************************************************************** */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
    
/* *****************************************************************************
 * Configuration Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Constants and Macros Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Enumeration Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Type Definitions
 **************************************************************************** */
typedef struct
{
    char cName[16];
    uint8_t* pStream;
    size_t nLength;
    bool bRingBuffer;
    SemaphoreHandle_t flag_available;
    size_t nLengthMax;
} drv_stream_t;

/* *****************************************************************************
 * Function-Like Macro
 **************************************************************************** */

/* *****************************************************************************
 * Variables External Usage
 **************************************************************************** */ 

/* *****************************************************************************
 * Function Prototypes
 **************************************************************************** */
void drv_stream_init(drv_stream_t* psStream, uint8_t* pBuffer, size_t nLength);
//size_t drv_stream_size(drv_stream_t* psStream);
size_t drv_stream_push(drv_stream_t* psStream, uint8_t* pData, size_t nSize);
size_t drv_stream_pull(drv_stream_t* psStream, uint8_t* pData, size_t nSize);
void drv_stream_list(void);
int drv_stream_get_position(const char* name);
drv_stream_t* drv_stream_get_handle(const char* name);
int drv_stream_get_size(drv_stream_t* pStream);
int drv_stream_get_free(drv_stream_t* pStream);


#ifdef __cplusplus
}
#endif /* __cplusplus */



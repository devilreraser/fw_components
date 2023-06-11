/* *****************************************************************************
 * File:   drv_stream.c
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
#include "drv_stream.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>


#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

/* *****************************************************************************
 * Configuration Definitions
 **************************************************************************** */
#define TAG "drv_stream"

/* *****************************************************************************
 * Constants and Macros Definitions
 **************************************************************************** */
#define DRV_STREAM_DEFAULT_LENGTH_MAX   4096    /* if 0 - no limit to test heap overflow */
#define DRV_STREAM_REMOVE_EXTRA_ON_SKIP 1024    /* count bytes additional to remove if skip data needed */

#define DRV_STREAM_COUNT_MAX            20

void esp_log_write_custom(esp_log_level_t level,
                   const char *tag,
                   const char *format, ...);
  
#define ESP_LOG_CUSTOM(level, tag, format, ...) do {                     \
        if (level==ESP_LOG_ERROR )          { esp_log_write_custom(ESP_LOG_ERROR,      tag, LOG_FORMAT(E, format), esp_log_timestamp(), tag, ##__VA_ARGS__); } \
        else if (level==ESP_LOG_WARN )      { esp_log_write_custom(ESP_LOG_WARN,       tag, LOG_FORMAT(W, format), esp_log_timestamp(), tag, ##__VA_ARGS__); } \
        else if (level==ESP_LOG_DEBUG )     { esp_log_write_custom(ESP_LOG_DEBUG,      tag, LOG_FORMAT(D, format), esp_log_timestamp(), tag, ##__VA_ARGS__); } \
        else if (level==ESP_LOG_VERBOSE )   { esp_log_write_custom(ESP_LOG_VERBOSE,    tag, LOG_FORMAT(V, format), esp_log_timestamp(), tag, ##__VA_ARGS__); } \
        else                                { esp_log_write_custom(ESP_LOG_INFO,       tag, LOG_FORMAT(I, format), esp_log_timestamp(), tag, ##__VA_ARGS__); } \
    } while(0)

#define ESP_LOG_LEVEL_CUSTOM(level, tag, format, ...) do {               \
        if ( LOG_LOCAL_LEVEL >= level ) ESP_LOG_CUSTOM(level, tag, format, ##__VA_ARGS__); \
    } while(0)

#define USE_LOG_DRV_STREAM 0    //    epic fail if enabled

#if USE_LOG_DRV_STREAM
#define ESP_LOGF( tag, format, ... ) ESP_LOG_LEVEL_CUSTOM(ESP_LOG_ERROR,   tag, format, ##__VA_ARGS__)
#define ESP_LOGP( tag, format, ... ) ESP_LOG_LEVEL_CUSTOM(ESP_LOG_WARN,    tag, format, ##__VA_ARGS__)
#define ESP_LOGN( tag, format, ... ) ESP_LOG_LEVEL_CUSTOM(ESP_LOG_INFO,    tag, format, ##__VA_ARGS__)
#else
#define ESP_LOGF( tag, format, ... )
#define ESP_LOGP( tag, format, ... )
#define ESP_LOGN( tag, format, ... )
#endif

/* *****************************************************************************
 * Enumeration Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Type Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Function-Like Macros
 **************************************************************************** */
int drv_stream_push_log_message(char* pdata, int nSize);
int drv_stream_pull_log_message(char* pdata, int nSize);
int log_vprintf_stream(const char *fmt, va_list args);
size_t stream_pull_internal(drv_stream_t* psStream, uint8_t* pData, size_t nSize);


/* *****************************************************************************
 * Variables Definitions
 **************************************************************************** */
static vprintf_like_t s_log_print_func = &vprintf;

char log_messages_stream_buffer[1024];
int log_messages_stream_buffer_fill = 0;
SemaphoreHandle_t log_messages_stream_buffer_available = NULL;

drv_stream_t* pStreamList[DRV_STREAM_COUNT_MAX] = {NULL};
int nStreamListCount = 0;
int nStreamCountTotal = 0;

/* *****************************************************************************
 * Prototype of functions definitions
 **************************************************************************** */

/* *****************************************************************************
 * Functions
 **************************************************************************** */
int log_vprintf_stream(const char *fmt, va_list args)
{
    size_t size_string;  
    size_string = vsnprintf(NULL, 0, fmt, args);

    char *string;
    string = (char *)malloc(size_string+1);
    vsnprintf(string, size_string+1, fmt, args);
    size_string = strlen(string);
    size_string = drv_stream_push_log_message(string, size_string);

    free(string);

    return size_string;
}


void esp_log_writev_custom(esp_log_level_t level,
                   const char *tag,
                   const char *format,
                   va_list args)
{
    // if (!esp_log_impl_lock_timeout()) 
    // {
    //     return;
    // }
    // esp_log_level_t level_for_tag = s_log_level_get_and_unlock(tag);
    // if (!should_output(level, level_for_tag)) {
    //     return;
    // }

    (*s_log_print_func)(format, args);

}

void esp_log_write_custom(esp_log_level_t level,
                   const char *tag,
                   const char *format, ...)
{
    va_list list;
    va_start(list, format);
    esp_log_writev_custom(level, tag, format, list);
    va_end(list);
}

int drv_stream_push_log_message(char* pdata, int nSize)
{
    int result = nSize;
    int totalNew = (log_messages_stream_buffer_fill + result);
    if (totalNew > sizeof(log_messages_stream_buffer))
    {
        drv_stream_pull_log_message(NULL, totalNew - sizeof(log_messages_stream_buffer));
    }
    xSemaphoreTake(log_messages_stream_buffer_available,portMAX_DELAY);
    memcpy(&log_messages_stream_buffer[log_messages_stream_buffer_fill], pdata, result);
    log_messages_stream_buffer_fill += result;
    xSemaphoreGive(log_messages_stream_buffer_available);
    return result;   
}

int drv_stream_pull_log_message(char* pdata, int nSize)
{
    int result = nSize;
    xSemaphoreTake(log_messages_stream_buffer_available,portMAX_DELAY);

    if (result > log_messages_stream_buffer_fill)
    {
        result = log_messages_stream_buffer_fill;
    }
    if (pdata)
    {
        memcpy(pdata, &log_messages_stream_buffer, result);
    }
    
    log_messages_stream_buffer_fill -= result;
    xSemaphoreGive(log_messages_stream_buffer_available);
    return result;   
}

void drv_stream_init(drv_stream_t* psStream, uint8_t* pBuffer, size_t nLength)
{


    if (log_messages_stream_buffer_available == NULL)
    {
        log_messages_stream_buffer_available = xSemaphoreCreateBinary();
        xSemaphoreGive(log_messages_stream_buffer_available);
    }


    if (psStream->flag_available == NULL)
    {
        psStream->flag_available = xSemaphoreCreateBinary();
    }
    else
    {
        xSemaphoreTake(psStream->flag_available, portMAX_DELAY);
    }

    bool bFound = false;
    for (int index = 0; index < DRV_STREAM_COUNT_MAX; index++)
    {
        if (pStreamList[index] == psStream)
        {
            bFound = true;
        }
    }
    if (bFound == false)
    {
        nStreamCountTotal++;
        if (nStreamListCount < DRV_STREAM_COUNT_MAX)
        {
            pStreamList[nStreamListCount++] = psStream;
        }
    }
    
    if (pBuffer == NULL)
    {
        psStream->bRingBuffer = false;
    }
    else
    {
        psStream->bRingBuffer = true;
    }
    psStream->pStream = NULL;
    psStream->nLength = 0;


    if (nLength > 0)
    {
        psStream->nLengthMax = nLength;
    }
    if (psStream->nLengthMax == 0)
    {
        psStream->nLengthMax = DRV_STREAM_DEFAULT_LENGTH_MAX;
    }
    xSemaphoreGive(psStream->flag_available);
}

// size_t drv_stream_size(drv_stream_t* psStream)
// {
//     if (psStream->flag_available == NULL)
//     {
//         ESP_LOGF(TAG, "Failure call drv_stream_init before drv_stream_pull");
//         return 0;
//     }
//     size_t nResult = 0;
//     xSemaphoreTake(psStream->flag_available, portMAX_DELAY);
//     if (psStream->pStream != NULL)
//     {
//         nResult = psStream->nLength;
//     }    
//     xSemaphoreGive(psStream->flag_available);
//     return nResult;
// }

size_t drv_stream_push(drv_stream_t* psStream, uint8_t* pData, size_t nSize)
{
    // if (psStream == NULL) 
    // {
    //     ESP_LOGE(TAG, "Failure call NULL stream drv_stream_push");
    //     return 0;
    // }

    if (psStream->flag_available == NULL)
    {
        ESP_LOGF(TAG, "Failure call drv_stream_init before drv_stream_push");
        return 0;
    }
    size_t nResult = 0;
    xSemaphoreTake(psStream->flag_available, portMAX_DELAY);
    /* limit stream buffer */
    if (psStream->nLengthMax > 0) 
    {
        if((nSize + psStream->nLength) > psStream->nLengthMax)
        {
            int bytesRemove = (nSize + psStream->nLength) - (psStream->nLengthMax - DRV_STREAM_REMOVE_EXTRA_ON_SKIP);
            ESP_LOGF(TAG, "Stream %s Skipped %d/%d bytes", psStream->cName, bytesRemove, nSize + psStream->nLength);
            stream_pull_internal(psStream, NULL, bytesRemove);
        }
        else if((nSize + psStream->nLength) > (psStream->nLengthMax - DRV_STREAM_REMOVE_EXTRA_ON_SKIP))
        {
            ESP_LOGP(TAG, "Stream %s Warning %d/%d bytes", psStream->cName, nSize + psStream->nLength, psStream->nLengthMax);
        }
    }
    if (psStream->bRingBuffer)
    {

    }
    else
    {
        uint8_t* pNewStream;
        if ((psStream->pStream != NULL) && (psStream->nLength > 0))
        {
            pNewStream = (uint8_t*)realloc(psStream->pStream, nSize + psStream->nLength);
        }
        else
        {
            psStream->nLength = 0;
            pNewStream = (uint8_t*)malloc(nSize);
        }
        if (pNewStream != NULL)
        {
            memcpy(&pNewStream[psStream->nLength], pData, nSize);
            psStream->pStream = pNewStream;
            psStream->nLength += nSize;
            nResult = nSize;
        }
        else
        {
            ESP_LOGF(TAG, "Failure not enough memory available for drv_stream_push");
        }

#if 0
        uint8_t* pNewStream = (uint8_t*)malloc(nSize + psStream->nLength);
        if (pNewStream != NULL)
        {
            if (psStream->pStream != NULL)
            {
                memcpy(pNewStream, psStream->pStream, psStream->nLength);
                free(psStream->pStream);
            }
            else
            {
                psStream->nLength = 0;
            }
            memcpy(&pNewStream[psStream->nLength], pData, nSize);
            psStream->pStream = pNewStream;
            psStream->nLength += nSize;
            nResult = nSize;
        }
        else
        {
            ESP_LOGF(TAG, "Failure not enough memory available for drv_stream_push");
        }
#endif

    }
    xSemaphoreGive(psStream->flag_available);
    return nResult;
}

size_t stream_pull_internal(drv_stream_t* psStream, uint8_t* pData, size_t nSize)
{
    size_t nResult = 0;

    if (psStream->bRingBuffer)
    {

    }
    else
    {
#if 1
        if (psStream->pStream != NULL)
        {

            nResult = psStream->nLength;

            if (nResult)
            {
                if (nResult > nSize)
                {
                    nResult = nSize;
                }
                if (pData)
                {
                    memcpy(pData, psStream->pStream, nResult);
                }

                uint8_t* pNewStream = NULL;
                if (psStream->nLength > nSize)
                {
                    psStream->nLength -= nSize;
                    memcpy(psStream->pStream, &psStream->pStream[nSize], psStream->nLength);
                    
                    pNewStream = (uint8_t*)realloc(psStream->pStream, psStream->nLength);
                    if (pNewStream == NULL)
                    {
                        ESP_LOGF(TAG, "Failure not enough memory available for drv_stream_pull");
                    }
                }
                else
                {
                    psStream->nLength = 0;
                    free(psStream->pStream);
                }
                psStream->pStream = pNewStream;
            }
            else
            {
                free(psStream->pStream);
            }
            
            
            
        }
        else
        {
            psStream->nLength = 0;
        }

#else
        if (psStream->pStream != NULL)
        {
            uint8_t* pNewStream = NULL;

            nResult = psStream->nLength;
            
            if (nResult > nSize)
            {
                psStream->nLength -= nSize;
                pNewStream = (uint8_t*)malloc(psStream->nLength);
                if (pNewStream != NULL)
                {
                    memcpy(pNewStream, &psStream->pStream[nSize], psStream->nLength);
                }
                else
                {
                    ESP_LOGF(TAG, "Failure not enough memory available for drv_stream_pull");
                }
                nResult = nSize;
            }
            else
            {
                psStream->nLength = 0;
            }
            if (pData)
            {
                memcpy(pData, psStream->pStream, nResult);
            }
            
            free(psStream->pStream);
            psStream->pStream = pNewStream;
            
        }
        #endif
    }
    return nResult;
}

size_t drv_stream_pull(drv_stream_t* psStream, uint8_t* pData, size_t nSize)
{
    if (psStream->flag_available == NULL)
    {
        ESP_LOGF(TAG, "Failure call drv_stream_init before drv_stream_pull");
        return 0;
    }
    xSemaphoreTake(psStream->flag_available, portMAX_DELAY);
    size_t nResult = stream_pull_internal(psStream, pData, nSize);
    xSemaphoreGive(psStream->flag_available);
    return nResult;
}

void drv_stream_list(void)
{
    ESP_LOGI(TAG, "Streams in list %d. Streams Total %d.", nStreamListCount, nStreamCountTotal);
    for (int index = 0; index < nStreamListCount; index++)
    {
        if (pStreamList[index] != NULL)
        {
            ESP_LOGI(TAG, "Success Stream[%2d] Name:%16s|Size:%5d/%5d bytes", index, pStreamList[index]->cName, pStreamList[index]->nLength, pStreamList[index]->nLengthMax);
        }
        else
        {
            ESP_LOGE(TAG, "Failure Stream[%d] Null", index);
        }
    }
}

int drv_stream_get_position(const char* name)
{
    int result = -1;
    for (int index = 0; index < nStreamListCount; index++)
    {
        if (pStreamList[index] != NULL)
        {
            if(strcmp(pStreamList[index]->cName, name) == 0)
            {
                result = index;
                break;
            }
        }
    }
    return result;
}

drv_stream_t* drv_stream_get_handle(const char* name)
{
    drv_stream_t* result = NULL;
    for (int index = 0; index < nStreamListCount; index++)
    {
        if (pStreamList[index] != NULL)
        {
            if(strcmp(pStreamList[index]->cName, name) == 0)
            {
                result = pStreamList[index];
                break;
            }
        }
    }
    return result;
}

int drv_stream_get_size(drv_stream_t* pStream)
{
    int result = -1;
    if (pStream != NULL)
    {
        result = pStream->nLength;
    }
    return result;
}
int drv_stream_get_free(drv_stream_t* pStream)
{
    int result = -1;
    if (pStream != NULL)
    {
        if (pStream->nLengthMax > 0) 
        {
            if(pStream->nLength <= pStream->nLengthMax)
            {
                result = pStream->nLengthMax - pStream->nLength;
            }
            else
            {
                result = -3;
            }
        }
        else
        {
            result = -2;
        }
    }
    return result;
}
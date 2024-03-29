#ifndef _ESP_MATH_OPT_H_
#define _ESP_MATH_OPT_H_  

#include "esp_idf_version.h"

/**
 * @brief Memory Alignment
 * 
 */
#define ALIGNMENT 16

/**
 * @brief Default Fractional bits for fixed point numbers
 * 
 */
#define FRACTIONAL 8

/*Uncomment to enable benchmark results via uart port.*/
//#define BENCHMARK_TEST


#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#define dsp_ENTER_CRITICAL      portSET_INTERRUPT_MASK_FROM_ISR
#define dsp_EXIT_CRITICAL       portCLEAR_INTERRUPT_MASK_FROM_ISR
#else
#define dsp_ENTER_CRITICAL      portENTER_CRITICAL_NESTED
#define dsp_EXIT_CRITICAL       portEXIT_CRITICAL_NESTED
#endif

#endif
#ifndef __PUSH_H__
#define __PUSH_H__

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

void push_open(QueueHandle_t in, QueueHandle_t out);
void push_start(void);
void push_stop(void);
void push_ready(void);
void push_exit(void);
void push_close(void);
void push_restart(void);

#ifdef __cplusplus
}
#endif

#endif /* __PUSH_H__ */

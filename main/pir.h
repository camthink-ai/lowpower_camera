#ifndef PIR_H_
#define PIR_H_

#include "stdio.h"

#define PIR_ENABLE          1

#define PIR_INTDOUT_IO      (2)//(20)
#define PIR_SERIAL_IO       (41)//(41)
#define PIR_IN_ACTIVE       (1)
#define PIR_INIT_RETRY      (10)


void pir_init(uint8_t is_first);
void pir_int_trigger(void);

#endif
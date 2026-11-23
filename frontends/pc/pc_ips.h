#ifndef PC_IPS_H
#define PC_IPS_H
#include <stddef.h>
typedef struct { const char *name; const unsigned char *data; size_t len; } PcIps;
extern const PcIps PC_IPS[];   /* NULL name ends the table */
#endif

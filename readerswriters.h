#ifndef READERSWRITERS_H
#define READERSWRITERS_H

typedef struct Monitor Monitor;

Monitor* init_monitor();

void free_monitor(Monitor* m);

void begin_write(Monitor* m);

void end_write(Monitor* m);

void begin_read(Monitor* m);

void end_read(Monitor* m);

#endif
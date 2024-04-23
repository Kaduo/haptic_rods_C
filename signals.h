#ifndef SIGNALS_H_
#define SIGNALS_H_

#include <stdint.h>

typedef enum  {
PLAY_PROTOCOL = 0x84,
CLEAR_PROTOCOL = 0x85,
ADD_SIGNAL_PROTOCOL = 0x83,
} Protocol;


typedef enum {
SINE = 0x02,
} SignalType;

typedef struct {
    SignalType signal_type;
    uint8_t amplitude;
    uint8_t offset;
    uint8_t duty;
    uint16_t period;
    uint16_t phase;
} Signal;

int set_interface_attribs(int fd, int speed);
void set_mincount(int fd, int mcount);
int connect_to_tty();
void write_to_tty(int fd, unsigned char *buffer, int buffer_len);
void set_signal(int fd, int8_t angle, int8_t pulses, Signal signal);
Signal signal_new(SignalType signal_type, uint8_t amplitude, uint8_t offset, uint8_t duty, uint8_t period, uint8_t phase);
void clear_signal(int fd);
#endif // SIGNALS_H_

#ifndef SIGNALS_H_
#define SIGNALS_H_

#include <stdint.h>

typedef enum  {
PING_PROTOCOL = 0x01,
SET_DIR_PROTOCOL = 0x82,
ADD_SIGNAL_PROTOCOL = 0x83,
PLAY_PROTOCOL = 0x84,
CLEAR_PROTOCOL = 0x85,
} Protocol;


typedef enum {
SINE = 0x02,
STEADY = 0x01,
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
Signal signal_new(SignalType signal_type, uint8_t amplitude, uint8_t offset, uint8_t duty, uint16_t period, uint16_t phase);
void clear_signal(int fd);
void play_signal(int fd, int play);
void ping(int fd);
void set_direction(int fd, int8_t angle, int16_t speed);
void add_signal(int fd, int8_t angle, int8_t pulses, Signal signal);
#endif // SIGNALS_H_

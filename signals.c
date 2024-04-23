#include "signals.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define TERMINAL "/dev/ttyUSB0"

int set_interface_attribs(int fd, int speed)
{
    struct termios tty;

    if (tcgetattr(fd, &tty) < 0) {
        printf("Error from tcgetattr: %s\n", strerror(errno));
        return -1;
    }

    cfsetospeed(&tty, (speed_t)speed);
    cfsetispeed(&tty, (speed_t)speed);

    tty.c_cflag |= (CLOCAL | CREAD);    /* ignore modem controls */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;         /* 8-bit characters */
    tty.c_cflag &= ~PARENB;     /* no parity bit */
    tty.c_cflag &= ~CSTOPB;     /* only need 1 stop bit */
    tty.c_cflag &= ~CRTSCTS;    /* no hardware flowcontrol */

    /* setup for non-canonical mode */
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tty.c_oflag &= ~OPOST;

    /* fetch bytes as they become available */
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        printf("Error from tcsetattr: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

void set_mincount(int fd, int mcount)
{
    struct termios tty;

    if (tcgetattr(fd, &tty) < 0) {
        printf("Error tcgetattr: %s\n", strerror(errno));
        return;
    }

    tty.c_cc[VMIN] = mcount ? 1 : 0;
    tty.c_cc[VTIME] = 5;        /* half second timer */

    if (tcsetattr(fd, TCSANOW, &tty) < 0)
        printf("Error tcsetattr: %s\n", strerror(errno));
}

int connect_to_tty() {
    char *portname = TERMINAL;

    int fd;
    fd = open(portname, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        printf("Error opening %s: %s\n", portname, strerror(errno));
        return -1;
    }
    /*baudrate 1000000, 8 bits, no parity, 1 stop bit */
    set_interface_attribs(fd, B1000000);

    return fd;
}

void write_to_tty(int fd, unsigned char *buffer, int buffer_len) {
    int wlen = write(fd, buffer, buffer_len);
    if (wlen != buffer_len) {
        printf("Error from write: %d, %d\n", wlen, errno);
    }
    tcdrain(fd);    /* delay for output */
}

void add_signal(int fd, int8_t angle, int8_t pulses, Signal signal) {
    #define ADD_BUFFER_LEN  11
    unsigned char buffer[ADD_BUFFER_LEN] = {
    (unsigned char)ADD_SIGNAL_PROTOCOL,
    (unsigned char)angle,
    (unsigned char)pulses,
    (unsigned char)signal.signal_type,
    (unsigned char)signal.amplitude,
    (unsigned char)signal.offset,
    (unsigned char)signal.duty,
    (unsigned char)signal.period,
    (unsigned char)(signal.period >> 8),
    (unsigned char)signal.phase,
    (unsigned char)(signal.phase >> 8),
};
    write_to_tty(fd, buffer, ADD_BUFFER_LEN);
}

void clear_signal(int fd) {
    #define CLEAR_BUFFER_LEN 1
    unsigned char buffer[CLEAR_BUFFER_LEN] = {CLEAR_PROTOCOL};
    write_to_tty(fd, buffer, CLEAR_BUFFER_LEN);
}

void ping(int fd) {
    #define PING_BUFFER_LEN 1
    unsigned char buffer[PING_BUFFER_LEN] = {PING_PROTOCOL};
    write_to_tty(fd, buffer, PING_BUFFER_LEN);
}

void play_signal(int fd, int play) {
    #define PLAY_BUFFER_LEN 2
    unsigned char buffer[PLAY_BUFFER_LEN] = {PLAY_PROTOCOL, play};
    write_to_tty(fd, buffer, PLAY_BUFFER_LEN);
}

void set_signal(int fd, int8_t angle, int8_t pulses, Signal signal) {
    clear_signal(fd);
    add_signal(fd, angle, pulses, signal);
    play_signal(fd, 1);
}

void set_direction(int fd, int8_t angle, int16_t speed) {
    #define DIR_BUFFER_LEN 4

    unsigned char buffer[DIR_BUFFER_LEN] = {SET_DIR_PROTOCOL, angle, speed, speed >> 8};
    write_to_tty(fd, buffer, DIR_BUFFER_LEN);
}

Signal signal_new(SignalType signal_type, uint8_t amplitude, uint8_t offset, uint8_t duty, uint16_t period, uint16_t phase) {
    Signal sig;
    sig.signal_type = signal_type;
    sig.amplitude = amplitude;
    sig.offset = offset;
    sig.duty = duty;
    sig.period = period;
    sig.phase = phase;
    return sig;
}

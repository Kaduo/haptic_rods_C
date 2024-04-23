#include "raylib.h"
#define TERMINAL    "/dev/ttyUSB0"

#include "signals.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

const int NB_RODS_MENU = 10;
const int UNIT_ROD_WIDTH = 30;
const int ROD_HEIGHT = 20;
const Color colors[] = {LIGHTGRAY, RED, GREEN, PURPLE, YELLOW, DARKGREEN, BLACK, BROWN, BLUE, ORANGE};

typedef struct Rod {
    Rectangle rect;
    Color color;
} Rod;

void DrawRods(Rod rods[], int nbRods) {
    int i;
    for (i=0; i<nbRods; i++) {
        DrawRectangleRec(rods[i].rect, rods[i].color);
    }
}

void InitRodsMenu(Rod rodsMenu[]) {
    int i;
    for (i = 0; i < NB_RODS_MENU; i++) {
        rodsMenu[i] = (Rod){.rect = {0, i*ROD_HEIGHT, (i+1)*UNIT_ROD_WIDTH, ROD_HEIGHT}, .color= colors[i]};
    }

}

int main(void)
{
    int fd;
    fd = connect_to_tty();

    ping(fd);

        unsigned char buf[2];
        int rdlen;

        rdlen = read(fd, buf, sizeof(buf) - 1);
        if (rdlen > 0) {
#ifdef DISPLAY_STRING
            buf[rdlen] = 0;
            printf("Read %d: \"%s\"\n", rdlen, buf);
#else /* display hex */
            unsigned char   *p;
            printf("Read %d:", rdlen);
            for (p = buf; rdlen-- > 0; p++)
                printf(" 0x%x", *p);
            printf("\n");
#endif
        } else if (rdlen < 0) {
            printf("Error from read: %d: %s\n", rdlen, strerror(errno));
        } else {  /* rdlen == 0 */
            printf("Timeout from read\n");
        }

    
    Signal signal = signal_new(
        SINE,
        100,
        0,
        0,
        30,
        0
);



    const int WINDOW_WIDTH = 1024;
    const int WINDOW_HEIGHT = 600;

    int selected = -1;
    int deltaX = 0;
    int deltaY = 0;

    Rod rodsMenu[NB_RODS_MENU];
    InitRodsMenu(rodsMenu);
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "HapticRods");

    // Main loop
    while (!WindowShouldClose())
    {
        BeginDrawing();
        ClearBackground(RAYWHITE);

        Vector2 mousePosition = GetMousePosition();

        // Selection logic
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int i;
            for (i=0; i < NB_RODS_MENU; i++) {
                if (CheckCollisionPointRec(mousePosition, rodsMenu[i].rect)) {
                    selected = i;
                    deltaX = rodsMenu[i].rect.x - mousePosition.x;
                    deltaY = rodsMenu[i].rect.y - mousePosition.y;

                    printf("hi there ! \n");
                    set_signal(fd, 0, 0, signal);
                    break;
                }
            }
        } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            selected = -1;
            clear_signal(fd);
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && selected >= 0) {
            rodsMenu[selected].rect.x = mousePosition.x + deltaX;
            rodsMenu[selected].rect.y = mousePosition.y + deltaY;
        }

        // Draw menu
        DrawRods(rodsMenu, NB_RODS_MENU);

        EndDrawing();
    }

    CloseWindow();

    return 0;
}

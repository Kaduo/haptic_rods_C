#include "raylib.h"
#include "raymath.h"
#define TERMINAL "/dev/ttyUSB0"

#include "signals.h"
#include "tinyexpr.h"
#include <fcntl.h>
#include <libconfig.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>

const int NB_RODS_MENU = 10;
const int UNIT_ROD_WIDTH = 30;
const int ROD_HEIGHT = 40;
const Color colors[] = {LIGHTGRAY, RED,   GREEN, PURPLE, YELLOW,
                        DARKGREEN, BLACK, BROWN, BLUE,   ORANGE};

typedef struct Rod {
  Rectangle rect;
  Color color;
} Rod;

void DrawRods(Rod rods[], int nbRods) {
  int i;
  for (i = 0; i < nbRods; i++) {
    DrawRectangleRec(rods[i].rect, rods[i].color);
  }
}

void InitRodsMenu(Rod rodsMenu[]) {
  int i;
  for (i = 0; i < NB_RODS_MENU; i++) {
    rodsMenu[i] =
        (Rod){.rect = {0, i * ROD_HEIGHT, (i + 1) * UNIT_ROD_WIDTH, ROD_HEIGHT},
              .color = colors[i]};
  }
}

int compute_speed(float delta_x, float delta_y, float *old_time) {
  int new_time = GetFrameTime();
  int speed;
  if ((*old_time != 0) && (new_time - *old_time != 0)) {
    speed = Vector2Length((Vector2){.x = delta_x, .y = delta_y}) /
            (GetFrameTime() - *old_time);
  } else {
    speed = 1000;
  }
  *old_time = new_time;
  return speed;
}

int compute_angle(float delta_x, float delta_y) {
  return Vector2Angle((Vector2){.x = 1, .y = 0},
                      (Vector2){.x = delta_x, .y = delta_y});
}

double clamp(double d, double min, double max) {
  const double t = d < min ? min : d;
  return t > max ? max : t;
}

config_t read_config(bool *err) {
  config_t cfg;
  config_init(&cfg);
  if (!config_read_file(&cfg, "config.cfg")) {
    fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg),
            config_error_line(&cfg), config_error_text(&cfg));
    config_destroy(&cfg);
    *err = true;
  } else {
    *err = false;
  }
  return cfg;
}

te_expr get_expr(config_t *cfg, char *expr_name, te_variable *vars) {
  const te_expr *expr = 0;
  const char *string_expr;
  int err = 0;
  if (config_lookup_string(cfg, expr_name, &string_expr)) {
    expr = te_compile(string_expr, vars, 1, &err);
  }
  return *expr;
}

int main(void) {
  int fd;
  fd = connect_to_tty();

  /* ping(fd); */
  set_direction(fd, 0, 300);

  /* Signal s = signal_new(SINE, 127, 0, 0, 10, 0); */
  /* set_signal(fd, -1, -1, s); */
  /* sleep(5); */
  /* printf("switch!\n"); */
  /* Signal s2 = signal_new(SINE, 127, 0, 0, 10, 3); */
  /* add_signal(fd, -1, -1, s2); */
  /* sleep(5); */
  /* printf("switch!\n"); */
  /* add_signal(fd, -1, -1, s2); */

  /*   unsigned char buf[2]; */
  /*   int rdlen; */

  /*   rdlen = read(fd, buf, sizeof(buf) - 1); */
  /*   if (rdlen > 0) { */
  /* #ifdef DISPLAY_STRING */
  /*     buf[rdlen] = 0; */
  /*     printf("Read %d: \"%s\"\n", rdlen, buf); */
  /* #else /\* display hex *\/ */
  /*     unsigned char *p; */
  /*     printf("Read %d:", rdlen); */
  /*     for (p = buf; rdlen-- > 0; p++) */
  /*       printf(" 0x%x", *p); */
  /*     printf("\n"); */
  /* #endif */
  /*   } else if (rdlen < 0) { */
  /*     printf("Error from read: %d: %s\n", rdlen, strerror(errno)); */
  /*   } else { /\* rdlen == 0 *\/ */
  /*     printf("Timeout from read\n"); */
  /*   } */

  bool config_error = false;
  config_t cfg = read_config(&config_error);
  if (config_error) {
    return (EXIT_FAILURE);
  }

  double *l = NULL;
  te_variable vars[] = {{"l", l}};

  printf("hi\n");
  te_expr period_expr = get_expr(&cfg, "period_expr", vars);
  printf("ouh\n");
  te_expr amplitude_expr = get_expr(&cfg, "amplitude_expr", vars);
  te_expr duty_expr = get_expr(&cfg, "duty_expr", vars);
  te_expr offset_expr  = get_expr(&cfg, "offset_expr ", vars);

  printf("hmm\n");
  char *signal_parameter_name = "signal";
  const char *signal_name;
  int signal = SINE;
  if (config_lookup_string(&cfg, signal_parameter_name, &signal_name)) {
    if (strcmp(signal_name, "sine") == 0) {
      signal = SINE;

    } else if (strcmp(signal_name, "steady") == 0) {
      signal = STEADY;
    }
  }
  printf("eh\n");

  Signal signals[NB_RODS_MENU];
  int i;
  for (i = 0; i < NB_RODS_MENU; i++) {
    *l = i;
    double amplitude = clamp(te_eval(&amplitude_expr), 0, 0xFF);
    double period = clamp(te_eval(&period_expr), 0, 0xFFFF);
    double duty = clamp(te_eval(&duty_expr), 0, 0xFF);
    double offset = clamp(te_eval(&offset_expr), 0, 0xFF);
    signals[i] = signal_new(signal, amplitude, offset, duty, period, 0);
  }

  int selected = -1;
  int deltaX = 0;
  int deltaY = 0;

  Rod rodsMenu[NB_RODS_MENU];
  InitRodsMenu(rodsMenu);
  int display = GetCurrentMonitor();
  InitWindow(GetMonitorWidth(display), GetMonitorHeight(display), "HapticRods");
  ToggleFullscreen();

  float time;
  time = 0;
  // Main loop
  while (!WindowShouldClose()) {
    BeginDrawing();
    ClearBackground(RAYWHITE);

    Vector2 mousePosition = GetMousePosition();

    // Selection logic
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      int i;
      for (i = 0; i < NB_RODS_MENU; i++) {
        if (CheckCollisionPointRec(mousePosition, rodsMenu[i].rect)) {
          selected = i;
          deltaX = rodsMenu[i].rect.x - mousePosition.x;
          deltaY = rodsMenu[i].rect.y - mousePosition.y;

          set_signal(fd, -1, -1, signals[i]);
          break;
        }
      }
    } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
      selected = -1;
      clear_signal(fd);
      play_signal(fd, 0);
    }

    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && selected >= 0) {
      float dx = mousePosition.x + deltaX - rodsMenu[selected].rect.x;
      float dy = mousePosition.y + deltaY - rodsMenu[selected].rect.y;
      rodsMenu[selected].rect.x = mousePosition.x + deltaX;
      rodsMenu[selected].rect.y = mousePosition.y + deltaY;
      set_direction(fd, compute_angle(dx, dy), compute_speed(dx, dy, &time));
    }

    // Draw menu
    DrawRods(rodsMenu, NB_RODS_MENU);

    EndDrawing();
  }

  CloseWindow();

  return 0;
}

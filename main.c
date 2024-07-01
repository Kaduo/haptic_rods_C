#include "raylib.h"
#include "raymath.h"
#define TERMINAL "/dev/ttyUSB0"

#include "signals.h"
#include "tinyexpr.h"
#include <fcntl.h>
#include <libconfig.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

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
    DrawRectangleLinesEx(rods[i].rect, 1., BLACK);
  }
}

void InitRodsMenu(Rod rodsMenu[], int width, int height, int shift) {
  int i;
  for (i = shift; i < 10*(shift+1); i++) {
    rodsMenu[i] =
        (Rod){.rect = {shift, i * ROD_HEIGHT, (i + 1) * UNIT_ROD_WIDTH, ROD_HEIGHT},
              .color = colors[i]};
  }
}

bool CollisionTopToBottom(Rectangle rect1, Rectangle rect2) {
  return ((rect2.x < rect1.x ) && (rect1.x < rect2.x + rect2.width)) ||
         ((rect2.x < rect1.x + rect1.width) && ( rect1.x + rect1.width < rect2.x + rect2.width));
}

int compute_speed(float delta_x, float delta_y, float *old_time) {
  float new_time = GetFrameTime();
  int speed;
  if ((*old_time != 0) && (new_time - *old_time != 0)) {
    speed = Vector2Length((Vector2){.x = delta_x, .y = delta_y}) /
            (GetFrameTime() - *old_time);
  } else {
    speed = 1000;
  }
  *old_time = new_time;
  return abs(speed);
}

int compute_angle(float delta_x, float delta_y) {
  return Vector2Angle((Vector2){.x = 1, .y = 0},
                      (Vector2){.x = delta_x, .y = delta_y});
}

double clamp(double d, double min, double max) {
  const double t = d < min ? min : d;
  return t > max ? max : t;
}

config_t read_config(bool *err, char *config_name) {
  config_t cfg;
  config_init(&cfg);
  if (!config_read_file(&cfg, config_name)) {
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

const double PARAMETER_NOT_SET = -10;

double get_per_rod_setting(config_setting_t *setting, char *expr_name) {
  const char *string_expr;
  int err = 0;
  if (config_setting_lookup_string(setting, expr_name, &string_expr)) {
    return te_interp(string_expr, &err);
  } else {
    return PARAMETER_NOT_SET;
  }
}

double is_len(double a, double b) {
  if (a == b) {
    return 1;
  }
  return 0;
}

void generate_signals(config_t cfg, Signal *buf, int count) {
  double l;
  te_variable vars[] = {{"l", &l}};

  te_expr period_expr = get_expr(&cfg, "period_expr", vars);
  te_expr amplitude_expr = get_expr(&cfg, "amplitude_expr", vars);
  te_expr duty_expr = get_expr(&cfg, "duty_expr", vars);
  te_expr offset_expr = get_expr(&cfg, "offset_expr", vars);

  char *signal_parameter_name = "signal";
  const char *signal_name;
  int signal = SINE;
  if (config_lookup_string(&cfg, signal_parameter_name, &signal_name)) {
    if (strcmp(signal_name, "sine") == 0) {
      signal = SINE;
    } else if (strcmp(signal_name, "steady") == 0) {
      signal = STEADY;
    } else if (strcmp(signal_name, "triangle") == 0) {
      signal = TRIANGLE;
    } else if (strcmp(signal_name, "front teeth") == 0) {
      signal = FRONT_TEETH;
    } else if (strcmp(signal_name, "back teeth") == 0) {
      signal = BACK_TEETH;
    }
  }

  int i;
  for (i = 0; i < count; i++) {
    l = i;
    double amplitude = clamp(te_eval(&amplitude_expr), 0, 0xFF);
    double period = clamp(te_eval(&period_expr), 0, 0xFFFF);
    double duty = clamp(te_eval(&duty_expr), 0, 0xFF);
    double offset = clamp(te_eval(&offset_expr), 0, 0xFF);
    buf[i] = signal_new(signal, amplitude, offset, duty, period, 0);
  }

  int per_rod = 0;
  config_lookup_bool(&cfg, "per_rod", &per_rod);

  if (per_rod) {
    char *rod_names[] = {"r1", "r2", "r3", "r4", "r5",
                         "r6", "r7", "r8", "r9", "r10"};
    int i;
    for (i = 0; i < 10; i++) {
      config_setting_t *setting = config_lookup(&cfg, rod_names[i]);

      if (setting != NULL) {

        double period = get_per_rod_setting(setting, "period");
        if (period != PARAMETER_NOT_SET) {
          buf[i].period = clamp(period, 0, 0xFFFF);
        }

        double amplitude = get_per_rod_setting(setting, "amplitude");
        if (amplitude != PARAMETER_NOT_SET) {
          buf[i].amplitude = clamp(amplitude, 0, 0xFF);
        }

        double offset = get_per_rod_setting(setting, "offset");
        if (offset != PARAMETER_NOT_SET) {
          buf[i].offset = clamp(offset, 0, 0xFF);
        }

        double duty = get_per_rod_setting(setting, "duty");
        if (duty != PARAMETER_NOT_SET) {
          buf[i].duty = clamp(duty, 0, 0xFF);
        }
      }
    }
  }
}

int main(void) {
  int fd;
  fd = connect_to_tty();
  int collision_frame_count = 0;

  double times[100]; // FIXME
  Vector2 positions[100];

  bool config_error = false;
  config_t cfg = read_config(&config_error, "config.cfg");
  if (config_error) {
    return (EXIT_FAILURE);
  }

  Signal signals[NB_RODS_MENU];
  generate_signals(cfg, signals, NB_RODS_MENU);

  int selected = -1;
  int deltaX = 0;
  int deltaY = 0;

  Rod rodsMenu[NB_RODS_MENU*3];
  int display = GetCurrentMonitor();
  InitRodsMenu(rodsMenu, GetMonitorWidth(display), GetMonitorHeight(display), 0);
  InitRodsMenu(rodsMenu, GetMonitorWidth(display), GetMonitorHeight(display), 11*UNIT_ROD_WIDTH);
  InitRodsMenu(rodsMenu, GetMonitorWidth(display), GetMonitorHeight(display), 22*UNIT_ROD_WIDTH);

  InitWindow(GetMonitorWidth(display), GetMonitorHeight(display), "HapticRods");
  ToggleFullscreen();

  float time;
  time = 0;
  int j = 0;
  int i;

  // Main loop
  while (!WindowShouldClose()) {
    BeginDrawing();
    ClearBackground(RAYWHITE);

    Vector2 mousePosition = GetMousePosition();

    // Selection logic
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      times[j] = GetTime();
      positions[j] = mousePosition;
      j++;
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
      bool collided = false;
      float dx = mousePosition.x + deltaX - rodsMenu[selected].rect.x;
      float dy = mousePosition.y + deltaY - rodsMenu[selected].rect.y;
      float old_x = rodsMenu[selected].rect.x;
      float old_y = rodsMenu[selected].rect.y;
      Rectangle rect1 = rodsMenu[selected].rect;

      rodsMenu[selected].rect.x = mousePosition.x + deltaX;
      rodsMenu[selected].rect.y = mousePosition.y + deltaY;

      for (i = 0; i < NB_RODS_MENU; i++) {

        Rectangle rect2 = rodsMenu[i].rect;

        if (CheckCollisionRecs(rodsMenu[selected].rect, rodsMenu[i].rect) &&
            i != selected) {

          if (CollisionTopToBottom(rect1, rect2) || CollisionTopToBottom(rect2, rect1)) {
            if (rect1.y < rect2.y) {
              rodsMenu[selected].rect.y = rect2.y - ROD_HEIGHT - 1;
            } else {

              rodsMenu[selected].rect.y = rect2.y + ROD_HEIGHT + 1;
            }
          } else {
            if (rect1.x < rect2.x) {

              rodsMenu[selected].rect.x = rect2.x - rect1.width - 1;
            } else {

              rodsMenu[selected].rect.x = rect2.x + rect2.width + 1;
            }
          }

          Signal sig = signals[selected];
          sig.offset = 0;

          clear_signal(fd);
          set_signal(fd, -1, -1, sig);
          play_signal(fd, 1);
          collided = true;
          if (collision_frame_count == 0) {
            Signal sig = signals[selected];
            collision_frame_count = 5;
            sig.offset = 255;
            set_signal(fd, -1, -1, sig);

          }

          if (CheckCollisionRecs(rodsMenu[selected].rect, rodsMenu[i].rect)) {
            rodsMenu[selected].rect.x = old_x;
            rodsMenu[selected].rect.y = old_y;

          }

        }
      }
      // Signal sig = signals[selected];
      if (collision_frame_count > 0) {
        collision_frame_count -= 1;
        if (collision_frame_count == 0) {
          set_signal(fd, -1, -1, signals[selected]);
        }
      }
      // /* clear_signal(fd); */
      // /* add_signal(fd, -1, -1, signals[selected]); */
      // set_signal(fd, -1, -1, sig);
      // play_signal(fd, 1);
      set_direction(fd, compute_angle(dx, dy), compute_speed(dx, dy, &time));
    }

    // Draw menu
    DrawRods(rodsMenu, NB_RODS_MENU*3);

    EndDrawing();
  }

  CloseWindow();

  return 0;
}

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
const int UNIT_ROD_WIDTH = 40;
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
  for (i = 0; i < 10; i++) {
    rodsMenu[i + shift] = (Rod){.rect = {shift * UNIT_ROD_WIDTH, i * (ROD_HEIGHT + 1),
                                         (i + 1) * UNIT_ROD_WIDTH, ROD_HEIGHT},
                                .color = colors[i]};
  }
}

void InitRods(Rod rods[], int nb_per_kind[]) {
  int i;
  int j;
  int k = 0;
  for (i = 0; i < 10; i++) {
    for (j = 0; j < nb_per_kind[i]; j++) {
      rods[k] = (Rod){.rect = {0, 0, (i + 1) * UNIT_ROD_WIDTH, ROD_HEIGHT},
                      .color = colors[i]};
      k += 1;
    }
  }
}

bool CollisionTopToBottom(Rectangle rect1, Rectangle rect2) {
  return ((rect2.x < rect1.x) && (rect1.x < rect2.x + rect2.width)) ||
         ((rect2.x < rect1.x + rect1.width) &&
          (rect1.x + rect1.width < rect2.x + rect2.width));
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

void generate_signals(config_t cfg, Signal *buf, int count) {
  double l;
  te_variable vars[] = {{"l", &l}};

  te_expr period_expr = get_expr(&cfg, "period_expr", vars);
  te_expr amplitude_expr = get_expr(&cfg, "amplitude_expr", vars);
  te_expr duty_expr = get_expr(&cfg, "duty_expr", vars);
  te_expr offset_expr = get_expr(&cfg, "offset_expr", vars);

  char *signal_parameter_name = "signal_type";
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
        const char *signal_name;
        int signal = SINE;
        if (config_setting_lookup_string(setting, signal_parameter_name,
                                         &signal_name)) {
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
          buf[i].signal_type = signal;
        }
      }
    }
  }

  int per_group = 0;
  config_lookup_bool(&cfg, "per_group", &per_group);
  if (per_group) {
    printf("hell oworld !!!\n\n");
    char *groups[] = {"g1-7",   "g2-4-8", "g3-6-9", "g2-4-8", "g5-10",
                      "g3-6-9", "g1-7",   "g2-4-8", "g3-6-9", "g5-10"};
    int i;
    for (i = 0; i < 10; i++) {
      config_setting_t *setting = config_lookup(&cfg, groups[i]);
      printf("%d here \n\n", i);

      if (setting != NULL) {
        printf("hi world!\n");

        double period = get_per_rod_setting(setting, "period");
        if (period != PARAMETER_NOT_SET) {
          printf("wellellwell\n");
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
        const char *signal_name;
        int signal = SINE;
        if (config_setting_lookup_string(setting, signal_parameter_name,
                                         &signal_name)) {
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
          buf[i].signal_type = signal;
        }
      }
    }
  }
}

int main(void) {
  int fd;
  fd = connect_to_tty();
  int collision_frame_count = 0;
  int no_collision_frame_count = 0;

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

  Rod rodsMenu[NB_RODS_MENU];
  int display = GetCurrentMonitor();
  InitRodsMenu(rodsMenu, GetMonitorWidth(display), GetMonitorHeight(display),
               0);
  /*InitRodsMenu(rodsMenu, GetMonitorWidth(display), GetMonitorHeight(display),
               10);
  InitRodsMenu(rodsMenu, GetMonitorWidth(display), GetMonitorHeight(display),
               20);*/

  InitWindow(GetMonitorWidth(display), GetMonitorHeight(display), "HapticRods");
  ToggleFullscreen();
  SetTargetFPS(40);

  set_direction(fd, 0, 100); // FIXME ?

  float time;
  time = 0;
  int j = 0;
  int i;

  bool newly_collided = true;
  bool collided = false;
  bool original_signal = true;

  // Main loop
  while (!WindowShouldClose()) {
    BeginDrawing();
    ClearBackground(RAYWHITE);

    Vector2 mousePosition = GetMousePosition();

    collided = false;
    // Selection logic
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      no_collision_frame_count = 3;
      times[j] = GetTime();
      positions[j] = mousePosition;
      j++;
      for (i = 0; i < NB_RODS_MENU; i++) {
        if (CheckCollisionPointRec(mousePosition, rodsMenu[i].rect)) {
          selected = i;
          deltaX = rodsMenu[i].rect.x - mousePosition.x;
          deltaY = rodsMenu[i].rect.y - mousePosition.y;

          set_signal(fd, -1, -1, signals[i % NB_RODS_MENU]);
          break;
        }
      }
    } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
      selected = -1;
      clear_signal(fd);
      collision_frame_count = 0;
      // play_signal(fd, 0); FIXME
    }

    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && selected >= 0) {
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
          printf("hihihi\n");

          collided = true;

          if (CollisionTopToBottom(rect1, rect2) ||
              CollisionTopToBottom(rect2, rect1)) {
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
        }

        if (no_collision_frame_count > 0) {
          no_collision_frame_count -= 1;
        } else if (collision_frame_count == 0 && newly_collided && collided) {
          Signal sig = signals[selected % NB_RODS_MENU];
          collision_frame_count =2;
          sig.offset = 255;
          set_signal(fd, -1, -1, sig);
        }
      }

      if (collided) {
        // Check that we didn't merge two rods by accident.
        for (i = 0; i < NB_RODS_MENU; i++) {

          if (CheckCollisionRecs(rodsMenu[selected].rect, rodsMenu[i].rect) &&
              i != selected) {
            rodsMenu[selected].rect.x = old_x;
            rodsMenu[selected].rect.y = old_y;
          }
        }
      }

      newly_collided = !collided;
      // Signal sig = signals[selected];
      if (collision_frame_count > 0) {
        collision_frame_count -= 1;
        if (collision_frame_count == 0) {
          Signal sig = signals[selected % NB_RODS_MENU];
          sig.offset = 0;
          original_signal = false;
          set_signal(fd, -1, -1, sig);
        }
      } else if (!collided && !original_signal) {
        printf("hello world (:\n");
        printf("%d \n", signals[selected % NB_RODS_MENU].amplitude);
        printf("%d \n", signals[selected % NB_RODS_MENU].period);
        printf("%d \n", signals[selected % NB_RODS_MENU].offset);
        original_signal = true;
        set_signal(fd, -1, -1, signals[selected % NB_RODS_MENU]);
      }
      // set_direction(fd, 0, 100); // FIXME ?

      //set_direction(fd, compute_angle(dx, dy), compute_speed(dx, dy, &time));
      //printf("%d %d", compute_angle(dx,dy), compute_speed(dx, dy, &time));
    }

    // Draw menu
    DrawRods(rodsMenu, NB_RODS_MENU);

    EndDrawing();
  }

  CloseWindow();

  return 0;
}

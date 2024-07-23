#include "raylib.h"
#include "raymath.h"
#define TERMINAL "/dev/ttyUSB0"

#include "signals.h"
#include "tinyexpr.h"
#include <fcntl.h>
#include <libconfig.h>
#include <stddef.h>
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
  int length;
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
    rodsMenu[i + shift] =
        (Rod){.rect = {shift * UNIT_ROD_WIDTH, i * (ROD_HEIGHT + 1),
                       (i + 1) * UNIT_ROD_WIDTH, ROD_HEIGHT},
              .color = colors[i],
  .length = i + 1};
  }
}

void InitRods(Rod rods[], int nbRodsPerLength[], int screenWidth) {
  int i;
  int j;
  int x = 0;
  int y = 0;
  int current_length = 0;
  int k = 0;
  for (i = 0; i < 10; i++) {
    current_length += UNIT_ROD_WIDTH;
    for (j = 0; j < nbRodsPerLength[i]; j++) {
      if (current_length + x > screenWidth) {
        x = 0;
        y += ROD_HEIGHT + 1;
      }
      rods[k] =
          (Rod){.rect = {x, y, current_length, ROD_HEIGHT}, .color = colors[i], .length = i + 1};
      k += 1;
      x += current_length + 1;
    }
  }
}

bool IsCollisionOnHorizontalAxis(Rectangle rect1, Rectangle rect2) {
  return ((rect2.x < rect1.x) && (rect1.x < rect2.x + rect2.width)) ||
         ((rect2.x < rect1.x + rect1.width) &&
          (rect1.x + rect1.width < rect2.x + rect2.width));
}

int ComputeSpeed(float deltaX, float deltaY, float *oldTime) {
  float newTime = GetFrameTime();
  int speed;
  if ((*oldTime != 0) && (newTime - *oldTime != 0)) {
    speed = Vector2Length((Vector2){.x = deltaX, .y = deltaY}) /
            (GetFrameTime() - *oldTime);
  } else {
    printf("ho come on\n");
    speed = 1000;
  }
  *oldTime = newTime;
  return abs(speed);
}

int ComputeAngle(float deltaX, float deltaY) {
  return Vector2Angle((Vector2){.x = 1, .y = 0},
                      (Vector2){.x = deltaX, .y = deltaY});
}

double DoubleClamp(double d, double min, double max) {
  const double t = d < min ? min : d;
  return t > max ? max : t;
}

config_t LoadConfig(bool *err, char *config_name) {
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

te_expr *GetConfigExpr(config_t *cfg, char *expr_name, te_variable *vars) {
  const char *string_expr;
  int err = 0;
  if (config_lookup_string(cfg, expr_name, &string_expr)) {
    return te_compile(string_expr, vars, 1, &err);
  }
  return 0;
}

const double PARAMETER_NOT_SET = -10;

double ReadParameterFromSetting(config_setting_t *setting, char *exprName) {
  const char *string_expr;
  int err = 0;
  if (config_setting_lookup_string(setting, exprName, &string_expr)) {
    return te_interp(string_expr, &err);
  } else {
    return PARAMETER_NOT_SET;
  }
}

void SetExpr16ParameterOfSignal(config_t *cfg, uint16_t *parameter, double l,
                                char *exprName, double mask) {
  double my_l = l;
  te_variable vars[] = {{"l", &my_l}};
  te_expr *expr = GetConfigExpr(cfg, exprName, vars);
  if ((void *)expr != 0) {
    *parameter = (uint16_t)DoubleClamp(te_eval(expr), 0, mask);
  }
}

void SetExpr8ParameterOfSignal(config_t *cfg, uint8_t *parameter, double l,
                               char *exprName, double mask) {
  double my_l = l;
  te_variable vars[] = {{"l", &my_l}};
  te_expr *expr = GetConfigExpr(cfg, exprName, vars);
  if ((void *)expr != 0) {
    *parameter = (uint8_t)DoubleClamp(te_eval(expr), 0, mask);
  }
  // printf("\n\n not there \n\n");
}

void SetSignalKind(config_t *cfg, SignalType *signalKind) {
  const char *signalName;
  if (config_lookup_string(cfg, "signal_type", &signalName)) {
    if (strcmp(signalName, "sine") == 0) {
      *signalKind = SINE;
    } else if (strcmp(signalName, "steady") == 0) {
      *signalKind = STEADY;
    } else if (strcmp(signalName, "triangle") == 0) {
      *signalKind = TRIANGLE;
    } else if (strcmp(signalName, "front teeth") == 0) {
      *signalKind = FRONT_TEETH;
    } else if (strcmp(signalName, "back teeth") == 0) {
      *signalKind = BACK_TEETH;
    }
  }
}

void InitSignals(config_t cfg, Signal signals[], int count, Rod rods[]) {

  char *signal_parameter_name = "signal_type";
  SignalType signal = SINE;
  SetSignalKind(&cfg, &signal);

  int i;
  for (i = 0; i < count; i++) {
    signals[i] = signal_new(signal, 0, 0, 0, 0, 0);
    SetExpr16ParameterOfSignal(
        &cfg, (uint16_t *)((char *)(&signals[i]) + offsetof(Signal, period)), rods[i].rect.width/UNIT_ROD_WIDTH,
        "period_expr", 0xFFFF);
    SetExpr8ParameterOfSignal(
        &cfg, (uint8_t *)((char *)(&signals[i]) + offsetof(Signal, amplitude)), rods[i].rect.width/UNIT_ROD_WIDTH,
         "amplitude_expr", 0xFF);
    SetExpr8ParameterOfSignal(
        &cfg, (uint8_t *)((char *)(&signals[i]) + offsetof(Signal, duty)), rods[i].rect.width/UNIT_ROD_WIDTH,
        "duty_expr", 0xFF);
    SetExpr8ParameterOfSignal(
        &cfg, (uint8_t *)((char *)(&signals[i]) + offsetof(Signal, offset)), rods[i].rect.width/UNIT_ROD_WIDTH,
        "offset_expr", 0xFF);
  }
  printf("\n upthere %d\n", signals[i].amplitude);

  int per_rod = 0;
  config_lookup_bool(&cfg, "per_rod", &per_rod);

  if (per_rod) {
    char *rod_names[] = {"r1", "r2", "r3", "r4", "r5",
                         "r6", "r7", "r8", "r9", "r10"};
    int i;
    for (i = 0; i < 10; i++) {
      config_setting_t *setting = config_lookup(&cfg, rod_names[i]);

      if (setting != NULL) {

        double period = ReadParameterFromSetting(setting, "period");
        if (period != PARAMETER_NOT_SET) {
          signals[i].period = DoubleClamp(period, 0, 0xFFFF);
        }

        double amplitude = ReadParameterFromSetting(setting, "amplitude");
        if (amplitude != PARAMETER_NOT_SET) {
          signals[i].amplitude = DoubleClamp(amplitude, 0, 0xFF);
        }

        double offset = ReadParameterFromSetting(setting, "offset");
        if (offset != PARAMETER_NOT_SET) {
          signals[i].offset = DoubleClamp(offset, 0, 0xFF);
        }

        double duty = ReadParameterFromSetting(setting, "duty");
        if (duty != PARAMETER_NOT_SET) {
          signals[i].duty = DoubleClamp(duty, 0, 0xFF);
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
          signals[i].signal_type = signal;
        }
      }
    }
  }

  int per_group = 0;
  config_lookup_bool(&cfg, "per_group", &per_group);
  if (per_group) {
    char *groups[] = {"g1-7",   "g2-4-8", "g3-6-9", "g2-4-8", "g5-10",
                      "g3-6-9", "g1-7",   "g2-4-8", "g3-6-9", "g5-10"};
    int i;
    for (i = 0; i < 10; i++) {
      config_setting_t *setting = config_lookup(&cfg, groups[i]);

      if (setting != NULL) {

        double period = ReadParameterFromSetting(setting, "period");
        if (period != PARAMETER_NOT_SET) {
          printf("\n Hey I'm set! \n");
          signals[i].period = DoubleClamp(period, 0, 0xFFFF);
        }

        double amplitude = ReadParameterFromSetting(setting, "amplitude");
        if (amplitude != PARAMETER_NOT_SET) {
          signals[i].amplitude = DoubleClamp(amplitude, 0, 0xFF);
        }

        double offset = ReadParameterFromSetting(setting, "offset");
        if (offset != PARAMETER_NOT_SET) {
          signals[i].offset = DoubleClamp(offset, 0, 0xFF);
        }

        double duty = ReadParameterFromSetting(setting, "duty");
        if (duty != PARAMETER_NOT_SET) {
          signals[i].duty = DoubleClamp(duty, 0, 0xFF);
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
          signals[i].signal_type = signal;
        }
      }
    }
  }
}

void SaveRods(Rod rods[], int nb_rods, FILE *file) {
  int i;
  fprintf(file, "%d ", nb_rods);
  for (i = 0; i < nb_rods; i++) {
    fprintf(file, "%d %f %f ", (int)(rods[i].rect.width) / (int)UNIT_ROD_WIDTH,
            rods[i].rect.x, rods[i].rect.y);
  }
}

void LoadRods(FILE *file, Rod rods[]) {
  int i;
  int nb_rods;
  fscanf(file, "%d ", &nb_rods);
  for (i = 0; i < nb_rods; i++) {
    Rod rod;
    int l;
    fscanf(file, "%d %f %f ", &l, &rod.rect.x, &rod.rect.y);
    rod.rect.width = UNIT_ROD_WIDTH * l;
    rod.rect.height = ROD_HEIGHT;
    rod.color = colors[l-1];
    rods[i] = rod;
  }
}

int main(int argc, char **argv) {
  int fd;
  fd = connect_to_tty();
  int collision_frame_count = 0;
  int no_collision_frame_count = 0;

  char *config_name;
  if (argc > 1) {
    config_name = argv[1];
  } else {
    config_name = "config.cfg";
  }

  double times[100]; // FIXME
  Vector2 positions[100];

  bool config_error = false;
  config_t cfg = LoadConfig(&config_error, config_name);
  if (config_error) {
    return (EXIT_FAILURE);
  }

  int selected = -1;
  int deltaX = 0;
  int deltaY = 0;

  int display = GetCurrentMonitor();
  // InitRodsMenu(rodsMenu, GetMonitorWidth(display), GetMonitorHeight(display),
  //             0);
  /*InitRodsMenu(rodsMenu, GetMonitorWidth(display), GetMonitorHeight(display),
               10);
  InitRodsMenu(rodsMenu, GetMonitorWidth(display), GetMonitorHeight(display),
               20);*/

  InitWindow(GetMonitorWidth(display), GetMonitorHeight(display), "HapticRods");
  ToggleFullscreen();
  SetTargetFPS(40);

  int nb_rods = 0;
  FILE *f;
  if (argc > 2) {
    f = fopen(argv[2], "r");
    fscanf(f, "%d ", &nb_rods);
    fclose(f);
  } else {

    int nb_rods_per_color[10] = {3, 2, 1, 4, 2, 1, 2, 2, 3, 4};

    int i;
    for (i = 0; i < 10; i++) {
      nb_rods += nb_rods_per_color[i];
    }
  }

  Rod rods[nb_rods];
  if (argc > 2) {
    f = fopen(argv[2], "r");
    LoadRods(f, rods);
    fclose(f);
  } else {
    int nb_rods_per_color[10] = {3, 2, 1, 4, 2, 1, 2, 2, 3, 4};
    InitRods(rods, nb_rods_per_color, GetMonitorWidth(display));
  }

  Signal signals[NB_RODS_MENU];
  InitSignals(cfg, signals, nb_rods, rods);
  set_direction(fd, 0, 100); // FIXME ?

  float time;
  time = 0;
  int j = 0;

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
      int i;
      for (i = 0; i < nb_rods; i++) {
        if (CheckCollisionPointRec(mousePosition, rods[i].rect)) {
          selected = i;
          deltaX = rods[i].rect.x - mousePosition.x;
          deltaY = rods[i].rect.y - mousePosition.y;

          /* if (selected == 2) { */
          /*   // FIXME FIXME */
          /*   printf("\n\n\n\n houatttt \n\n\n"); */
          /*   f = fopen("WHATATAT.rods", "w"); */
          /*   if (f == NULL) { */
          /*     // Error, as expected. */
          /*     perror("Error opening file"); */
          /*     exit(-1); */
          /*   } */
          /*   SaveRods(rods, nb_rods, f); */
          /*   fclose(f); */
          /* } */

          set_signal(fd, -1, -1, signals[rods[i].length - 1]);
          printf("\n period %d\n\n", signals[rods[i].length - 1].period);
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
      float dx = mousePosition.x + deltaX - rods[selected].rect.x;
      float dy = mousePosition.y + deltaY - rods[selected].rect.y;
      float oldX = rods[selected].rect.x;
      float oldY = rods[selected].rect.y;
      Rectangle rect1 = rods[selected].rect;

      rods[selected].rect.x = mousePosition.x + deltaX;
      rods[selected].rect.y = mousePosition.y + deltaY;

      int i;
      for (i = 0; i < nb_rods; i++) {

        Rectangle rect2 = rods[i].rect;

        if (CheckCollisionRecs(rods[selected].rect, rods[i].rect) &&
            i != selected) {

          collided = true;

          if (IsCollisionOnHorizontalAxis(rect1, rect2) ||
              IsCollisionOnHorizontalAxis(rect2, rect1)) {
            if (rect1.y < rect2.y) {
              rods[selected].rect.y = rect2.y - ROD_HEIGHT - 1;
            } else {

              rods[selected].rect.y = rect2.y + ROD_HEIGHT + 1;
            }
          } else {
            if (rect1.x < rect2.x) {

              rods[selected].rect.x = rect2.x - rect1.width - 1;
            } else {
              rods[selected].rect.x = rect2.x + rect2.width + 1;
            }
          }
        }

        if (no_collision_frame_count > 0) {
          no_collision_frame_count -= 1;
        } else if (collision_frame_count == 0 && newly_collided && collided) {
          Signal sig = signals[selected % NB_RODS_MENU];
          collision_frame_count = 2;
          sig.offset = 255;
          set_signal(fd, -1, -1, sig);
        }
      }

      if (collided) {
        // Check that we didn't merge two rods by accident.
        for (i = 0; i < nb_rods; i++) {

          if (CheckCollisionRecs(rods[selected].rect, rods[i].rect) &&
              i != selected) {
            rods[selected].rect.x = oldX;
            rods[selected].rect.y = oldY;
          }
        }
      }

      newly_collided = !collided;
      // Signal sig = signals[selected];
      if (collision_frame_count > 0) {
        collision_frame_count -= 1;
        if (collision_frame_count == 0) {
          original_signal = false;
          clear_signal(fd);
        }
      } else if (!collided && !original_signal) {
        original_signal = true;
        set_signal(fd, -1, -1, signals[selected % NB_RODS_MENU]);
      }
      // set_direction(fd, 0, 100); // FIXME ?

      //set_direction(fd, ComputeAngle(dx, dy), ComputeSpeed(dx, dy, &time)); // FIXME
      set_direction(fd, 0, ComputeSpeed(dx, dy, &time)); // FIXME
      // printf("%d %d", compute_angle(dx,dy), compute_speed(dx, dy, &time));
    }

    // Draw menu
    DrawRods(rods, nb_rods);

    EndDrawing();
  }

  CloseWindow();

  return 0;
}

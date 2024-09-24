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
const int SELECTION_COUNTDOWN = 3;
const Color COLORS[] = {LIGHTGRAY, RED, GREEN, PURPLE, YELLOW,
                        DARKGREEN, BLACK, BROWN, BLUE, ORANGE};

const double PARAMETER_NOT_SET = -10;

static const char DEFAULT_CONFIG[] = "config.cfg";
static const char DEFAULT_SPEC[] = "spec.rods";

typedef struct Rod
{
  Rectangle rect;
  Color color;
  int length;
} Rod;

typedef struct RodGroup
{
  int nb_rods;
  Rod rods[];
} RodGroup;

typedef struct SelectionState {
  bool selected;
  Rod *selectedRod;
  int selectionCountdown;
  int offsetX;
  int offsetY;
} SelectionState;

SelectionState InitSelectionState() {
  SelectionState s;
  s.selected = false;
  s.selectionCountdown = 0;
  s.offsetX = 0;
  s.offsetY = 0;
  return  s;
}

void SelectRod(SelectionState *s, Rod *rod, Vector2 mousePosition) {
  s->selected = true;
  s->selectedRod = rod;
  s->selectionCountdown = SELECTION_COUNTDOWN;
  s->offsetX = rod->rect.x - mousePosition.x;
  s->offsetY = rod->rect.y - mousePosition.y;
}

void UnselectRod(SelectionState *s) {
  s->selected = false;
  s->selectionCountdown = 0;
}

typedef struct CollisionState {
  
} CollisionState;

// void UpdateSelectionState(SelectionState *s, RodGroup *rodGroup, Vector2 mousePosition) {
//     if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
//     {

//       /* Right after selecting a rod, there is a short period during
//       which its signal is guaranteed to play, even if there is a collision.*/
//       signalMustPlayFrameCount = 3;

//       // For each rod, check if it's under the mouse.
//       int i;
//       for (i = 0; i < rodGroup->nb_rods; i++)
//       {
//         if (CheckCollisionPointRec(mousePosition, rodGroup->rods[i].rect))
//         {
//           s->selectedIdx = i;
//           s->offsetX = rodGroup->rods[i].rect.x - mousePosition.x;
//           s->offsetY = rodGroup->rods[i].rect.y - mousePosition.y;

//           set_signal(fd, -1, -1, signals[rods[i].length - 1]);
//           break;
//         }
//       }
//     }
//     if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
//     {
//       selected = -1;
//       clear_signal(fd);
//       collision_frame_count = 0;
//     }

// }

Rod CreateRod(int l, float x, float y) {
  Rod rod;
  rod.rect.x = x;
  rod.rect.y = y;
  rod.rect.width = UNIT_ROD_WIDTH * l;
  rod.rect.height = ROD_HEIGHT;
  rod.length = l;
  rod.color = COLORS[l - 1];
  return rod;
}

void DrawRods(Rod rods[], int nbRods)
{
  int i;
  for (i = 0; i < nbRods; i++)
  {
    DrawRectangleRec(rods[i].rect, rods[i].color);
    DrawRectangleLinesEx(rods[i].rect, 1., BLACK);
  }
}

// Deprecated
void InitRodsMenu(Rod rodsMenu[], int width, int height, int shift)
{
  int i;
  for (i = 0; i < 10; i++)
  {
    rodsMenu[i + shift] =
        (Rod){.rect = {shift * UNIT_ROD_WIDTH, i * (ROD_HEIGHT + 1),
                       (i + 1) * UNIT_ROD_WIDTH, ROD_HEIGHT},
              .color = COLORS[i],
              .length = i + 1};
  }
}

void InitRods(Rod rods[], int nbRodsPerLength[], int screenWidth)
{
  int i;
  int j;
  int x = 0;
  int y = 0;
  int current_length = 0;
  int k = 0;
  for (i = 0; i < 10; i++)
  {
    current_length += UNIT_ROD_WIDTH;
    for (j = 0; j < nbRodsPerLength[i]; j++)
    {
      if (current_length + x > screenWidth)
      {
        x = 0;
        y += ROD_HEIGHT + 1;
      }
      rods[k] = (Rod){.rect = {x, y, current_length, ROD_HEIGHT},
                      .color = COLORS[i],
                      .length = i + 1};
      k += 1;
      x += current_length + 1;
    }
  }
}

bool IsCollisionOnHorizontalAxis(Rectangle rect1, Rectangle rect2)
{
  return ((rect2.x <= rect1.x) && (rect1.x <= rect2.x + rect2.width)) ||
         ((rect2.x <= rect1.x + rect1.width) &&
          (rect1.x + rect1.width <= rect2.x + rect2.width));
}

int ComputeSpeed(float deltaX, float deltaY, float *oldTime)
{
  float newTime = GetTime();
  int speed;
  float speedf;
  if ((*oldTime != 0) && (newTime - *oldTime != 0))
  {
    speedf = Vector2Length((Vector2){.x = deltaX, .y = deltaY}) /
             (newTime - *oldTime);
    speed = floor(speedf);
  }
  else
  {
    speed = 1000;
  }
  *oldTime = newTime;
  return abs(speed);
}

int ComputeAngle(float deltaX, float deltaY)
{
  return Vector2Angle((Vector2){.x = 1, .y = 0},
                      (Vector2){.x = deltaX, .y = deltaY});
}

double ClampDouble(double d, double min, double max)
{
  const double t = d < min ? min : d;
  return t > max ? max : t;
}

config_t LoadConfig(bool *err, const char *config_name)
{
  config_t cfg;
  config_init(&cfg);
  if (!config_read_file(&cfg, config_name))
  {
    fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg),
            config_error_line(&cfg), config_error_text(&cfg));
    config_destroy(&cfg);
    *err = true;
  }
  else
  {
    *err = false;
  }
  return cfg;
}

te_expr *GetConfigExpr(config_t *cfg, char *expr_name, te_variable *vars)
{
  const char *string_expr;
  int err = 0;
  if (config_lookup_string(cfg, expr_name, &string_expr))
  {
    return te_compile(string_expr, vars, 1, &err);
  }
  return 0;
}

double ReadParameterFromSetting(config_setting_t *setting, char *exprName)
{
  const char *string_expr;
  int err = 0;
  if (config_setting_lookup_string(setting, exprName, &string_expr))
  {
    return te_interp(string_expr, &err);
  }
  else
  {
    return PARAMETER_NOT_SET;
  }
}

void SetExpr16ParameterOfSignal(config_t *cfg, uint16_t *parameter, double l,
                                char *exprName, double mask)
{
  double my_l = l;
  te_variable vars[] = {{"l", &my_l}};
  te_expr *expr = GetConfigExpr(cfg, exprName, vars);
  if ((void *)expr != 0)
  {
    *parameter = (uint16_t)ClampDouble(te_eval(expr), 0, mask);
  }
}

void SetExpr8ParameterOfSignal(config_t *cfg, uint8_t *parameter, double l,
                               char *exprName, double mask)
{
  double my_l = l;
  te_variable vars[] = {{"l", &my_l}};
  te_expr *expr = GetConfigExpr(cfg, exprName, vars);
  if ((void *)expr != 0)
  {
    *parameter = (uint8_t)ClampDouble(te_eval(expr), 0, mask);
  }
}

void SetSignalKind(config_t *cfg, SignalType *signalKind)
{
  const char *signalName;
  if (config_lookup_string(cfg, "signal_type", &signalName))
  {
    if (strcmp(signalName, "sine") == 0)
    {
      *signalKind = SINE;
    }
    else if (strcmp(signalName, "steady") == 0)
    {
      *signalKind = STEADY;
    }
    else if (strcmp(signalName, "triangle") == 0)
    {
      *signalKind = TRIANGLE;
    }
    else if (strcmp(signalName, "front teeth") == 0)
    {
      *signalKind = FRONT_TEETH;
    }
    else if (strcmp(signalName, "back teeth") == 0)
    {
      *signalKind = BACK_TEETH;
    }
  }
}

void InitSignals(config_t cfg, Signal signals[])
{

  char *signal_parameter_name = "signal_type";
  SignalType signal = SINE;
  SetSignalKind(&cfg, &signal);

  int i;
  for (i = 0; i < 10; i++)
  {
    signals[i] = signal_new(signal, 0, 0, 0, 0, 0);
    SetExpr16ParameterOfSignal(
        &cfg, (uint16_t *)((void *)(&signals[i]) + offsetof(Signal, period)), i,
        "period_expr", 0xFFFF);
    SetExpr8ParameterOfSignal(
        &cfg, (uint8_t *)((void *)(&signals[i]) + offsetof(Signal, amplitude)),
        i, "amplitude_expr", 0xFF);
    SetExpr8ParameterOfSignal(
        &cfg, (uint8_t *)((void *)(&signals[i]) + offsetof(Signal, duty)), i,
        "duty_expr", 0xFF);
    SetExpr8ParameterOfSignal(
        &cfg, (uint8_t *)((void *)(&signals[i]) + offsetof(Signal, offset)), i,
        "offset_expr", 0xFF);
  }

  int per_rod = 0;
  config_lookup_bool(&cfg, "per_rod", &per_rod);

  if (per_rod)
  {
    char *rod_names[] = {"r1", "r2", "r3", "r4", "r5",
                         "r6", "r7", "r8", "r9", "r10"};
    int i;
    for (i = 0; i < 10; i++)
    {
      config_setting_t *setting = config_lookup(&cfg, rod_names[i]);

      if (setting != NULL)
      {

        double period = ReadParameterFromSetting(setting, "period");
        if (period != PARAMETER_NOT_SET)
        {
          signals[i].period = ClampDouble(period, 0, 0xFFFF);
        }

        double amplitude = ReadParameterFromSetting(setting, "amplitude");
        if (amplitude != PARAMETER_NOT_SET)
        {
          signals[i].amplitude = ClampDouble(amplitude, 0, 0xFF);
        }

        double offset = ReadParameterFromSetting(setting, "offset");
        if (offset != PARAMETER_NOT_SET)
        {
          signals[i].offset = ClampDouble(offset, 0, 0xFF);
        }

        double duty = ReadParameterFromSetting(setting, "duty");
        if (duty != PARAMETER_NOT_SET)
        {
          signals[i].duty = ClampDouble(duty, 0, 0xFF);
        }
        const char *signal_name;
        int signal = SINE;
        if (config_setting_lookup_string(setting, signal_parameter_name,
                                         &signal_name))
        {
          if (strcmp(signal_name, "sine") == 0)
          {
            signal = SINE;
          }
          else if (strcmp(signal_name, "steady") == 0)
          {
            signal = STEADY;
          }
          else if (strcmp(signal_name, "triangle") == 0)
          {
            signal = TRIANGLE;
          }
          else if (strcmp(signal_name, "front teeth") == 0)
          {
            signal = FRONT_TEETH;
          }
          else if (strcmp(signal_name, "back teeth") == 0)
          {
            signal = BACK_TEETH;
          }
          signals[i].signal_type = signal;
        }
      }
    }
  }

  int per_group = 0;
  config_lookup_bool(&cfg, "per_group", &per_group);
  if (per_group)
  {
    char *groups[] = {"g1-7", "g2-4-8", "g3-6-9", "g2-4-8", "g5-10",
                      "g3-6-9", "g1-7", "g2-4-8", "g3-6-9", "g5-10"};
    int i;
    for (i = 0; i < 10; i++)
    {
      config_setting_t *setting = config_lookup(&cfg, groups[i]);

      if (setting != NULL)
      {

        double period = ReadParameterFromSetting(setting, "period");
        if (period != PARAMETER_NOT_SET)
        {
          signals[i].period = ClampDouble(period, 0, 0xFFFF);
        }

        double amplitude = ReadParameterFromSetting(setting, "amplitude");
        if (amplitude != PARAMETER_NOT_SET)
        {
          signals[i].amplitude = ClampDouble(amplitude, 0, 0xFF);
        }

        double offset = ReadParameterFromSetting(setting, "offset");
        if (offset != PARAMETER_NOT_SET)
        {
          signals[i].offset = ClampDouble(offset, 0, 0xFF);
        }

        double duty = ReadParameterFromSetting(setting, "duty");
        if (duty != PARAMETER_NOT_SET)
        {
          signals[i].duty = ClampDouble(duty, 0, 0xFF);
        }
        const char *signal_name;
        int signal = SINE;
        if (config_setting_lookup_string(setting, signal_parameter_name,
                                         &signal_name))
        {
          if (strcmp(signal_name, "sine") == 0)
          {
            signal = SINE;
          }
          else if (strcmp(signal_name, "steady") == 0)
          {
            signal = STEADY;
          }
          else if (strcmp(signal_name, "triangle") == 0)
          {
            signal = TRIANGLE;
          }
          else if (strcmp(signal_name, "front teeth") == 0)
          {
            signal = FRONT_TEETH;
          }
          else if (strcmp(signal_name, "back teeth") == 0)
          {
            signal = BACK_TEETH;
          }
          signals[i].signal_type = signal;
        }
      }
    }
  }
}

void SaveRods(Rod rods[], int nb_rods, FILE *file)
{
  int i;
  fprintf(file, "%d ", nb_rods);
  for (i = 0; i < nb_rods; i++)
  {
    fprintf(file, "%d %f %f ", rods[i].length, rods[i].rect.x, rods[i].rect.y);
  }
}

// Deprecated, use CreateRodGroupFromSpec instead
void LoadRods(FILE *file, Rod rods[])
{
  int i;
  int nb_rods;
  fscanf(file, "%d ", &nb_rods);
  for (i = 0; i < nb_rods; i++)
  {
    Rod rod;
    int l;
    float x;
    float y;
    fscanf(file, "%d %f %f ", &l, &x, &y);
    rod.rect.x = x;
    rod.rect.y = y;
    rod.rect.width = UNIT_ROD_WIDTH * l;
    rod.rect.height = ROD_HEIGHT;
    rod.color = COLORS[l - 1];
    rods[i] = rod;
  }
}

RodGroup *CreateRodGroupFromSpec(const char *spec_name)
{
  int i;
  int nb_rods;
  FILE *f;
  f = fopen(spec_name, "r");
  fscanf(f, "%d ", &nb_rods);
  RodGroup *rod_group = malloc(sizeof(RodGroup) + nb_rods * sizeof(Rod));
  rod_group->nb_rods = nb_rods;
  for (i = 0; i < nb_rods; i++)
  {
    int l;
    float x;
    float y;
    fscanf(f, "%d %f %f ", &l, &x, &y);
    rod_group->rods[i] = CreateRod(l, x, y);
  }
  fclose(f);
  return rod_group;
}

int main(int argc, char **argv)
{

  // Establish connection to haptic controller
  int fd;
  fd = connect_to_tty();

  // The haptic signal won't play if no direction is set.
  set_direction(fd, 0, 10); // The value was chosen arbitrarily.


  // Load config -->
  const char *config_name;
  if (argc > 1)
  {
    config_name = argv[1];
  }
  else
  {
    config_name = DEFAULT_CONFIG;
  }
  bool config_error = false;
  config_t cfg = LoadConfig(&config_error, config_name);
  if (config_error)
  {
    return (EXIT_FAILURE);
  }
  // <-- Load config

  // Create rods -->
  const char *spec_name;
  if (argc > 2)
  {
    spec_name = argv[2];
  }
  else
  {
    spec_name = DEFAULT_SPEC;
  }

  RodGroup *rod_group = CreateRodGroupFromSpec(spec_name);
  Rod *rods = rod_group->rods;
  int nb_rods = rod_group->nb_rods;
  // <-- Create rods


  Signal signals[NB_RODS_MENU];
  InitSignals(cfg, signals);


  float time;
  time = GetTime();

  bool newlyCollided = true;
  bool collided = false;
  bool originalSignal = true;
  int selected = -1;
  
  int pointerOffsetX = 0; // Offset between pointer and selected rod 
  int pointerOffsetY = 0;

  int collision_frame_count = 0;
  int signalMustPlayFrameCount = 0;


  int display = GetCurrentMonitor();
  InitWindow(GetMonitorWidth(display), GetMonitorHeight(display), "HapticRods");
  printf("\n MonitorWidth: %d, MonitorHeight: %d", GetMonitorWidth(display), GetMonitorHeight(display));
  //InitWindow(300, 300, "HapticRods");
  #ifdef FULLSCREEN
    ToggleFullscreen();
  #endif
  SetTargetFPS(40);

  // Main loop
  while (!WindowShouldClose())
  {
    BeginDrawing();
    ClearBackground(RAYWHITE);

    Vector2 mousePosition = GetMousePosition();

    collided = false;

    // Selection logic -->
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {

      /* Right after selecting a rod, there is a short period during
      which its signal is guaranteed to play, even if there is a collision.*/
      signalMustPlayFrameCount = 3;

      // For each rod, check if it's under the mouse.
      int i;
      for (i = 0; i < nb_rods; i++)
      {

        // If a rod is under the mouse, mark it as selected.
        if (CheckCollisionPointRec(mousePosition, rods[i].rect))
        {
          selected = i;

          // Record the distance between the rod's origin and the mouse pointer.
          // As long as the rod is selected, this distance will be kept constant
          // on the axes along which the rod is free to move.
          pointerOffsetX = rods[i].rect.x - mousePosition.x;
          pointerOffsetY = rods[i].rect.y - mousePosition.y;

          set_signal(fd, -1, -1, signals[rods[i].length - 1]);
          break;
        }
      }
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
      selected = -1;
      clear_signal(fd);
      collision_frame_count = 0;
    }
    // <-- Selection logic

    // Movement logic ->>

    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && selected >= 0)
    {

      // Record the pointer's displacement to compute its speed.
      // Its speed is then used to compute the signal.
      float dx = mousePosition.x + pointerOffsetX - rods[selected].rect.x;
      float dy = mousePosition.y + pointerOffsetY - rods[selected].rect.y;

      // Record the current position of the selected rectangle,
      // so that it may be used to undo the move in case of an accidental merging.
      float oldX = rods[selected].rect.x;
      float oldY = rods[selected].rect.y;

      // TODO: cleanup: rename rect1 (selectedRect ?), and test if we can use it everywhere
      // instead of the verbose rods[selected].rect.
      // Furthermore, see if we can do the same with the rod itself, 
      // something like selectedRod = rods[selected].
      Rectangle rect1 = rods[selected].rect;

      // Move the selected rod so that it sticks to the pointer.
      rods[selected].rect.x = mousePosition.x + pointerOffsetX;
      rods[selected].rect.y = mousePosition.y + pointerOffsetY;

      // For each rod, check if it is colliding with the selected rod.
      int i;
      for (i = 0; i < nb_rods; i++)
      {

        Rectangle rect2 = rods[i].rect;

        if (CheckCollisionRecs(rods[selected].rect, rods[i].rect) &&
            i != selected)
        {

          collided = true;

          if (IsCollisionOnHorizontalAxis(rect1, rect2) ||
              IsCollisionOnHorizontalAxis(rect2, rect1))
          {


            if (rect1.y <= rect2.y) // The selected rod is colliding from above.
            {
              rods[selected].rect.y = rect2.y - ROD_HEIGHT;
            }
            else // The selected rod is colliding from below.
            {
              rods[selected].rect.y = rect2.y + ROD_HEIGHT;
            }
          }
          else
          {
            if (rect1.x <= rect2.x) { // The selected rod is colliding from the left.
              rods[selected].rect.x = rect2.x - rect1.width;
            }
            else // The selected rod is colliding from the right.
            {
              rods[selected].rect.x = rect2.x + rect2.width;
            }
          }
        }

        // Decrement the counter that prevents the signal from being played, even in the case of a collision.
        if (signalMustPlayFrameCount > 0) 
        {
          signalMustPlayFrameCount -= 1;
        }

        // If a new collision has just occurred, 
        else if (collision_frame_count == 0 && newlyCollided && collided)
        {
          // TODO: sig should be defined back when the rod is selected.
          Signal sig = signals[rods[selected].length - 1];

          collision_frame_count = 2;
          sig.offset = 255;
          set_signal(fd, -1, -1, sig);
        }
      }

      if (collided)
      {
        // Check that we didn't merge two rods by accident.
        for (i = 0; i < nb_rods; i++)
        {

          if (CheckCollisionRecs(rods[selected].rect, rods[i].rect) &&
              i != selected)
          {
            rods[selected].rect.x = oldX;
            rods[selected].rect.y = oldY;
          }
        }
      }

      newlyCollided = !collided;

      if (collision_frame_count > 0)
      {
        collision_frame_count -= 1;
        if (collision_frame_count == 0)
        {
          originalSignal = false;
          clear_signal(fd);
        }
      }
      else if (!collided && !originalSignal)
      {
        originalSignal = true;

        // TODO: clean this up. There should be a helper function,
        //  something like playSelectionSignal?
        set_signal(fd, -1, -1, signals[rods[selected].length - 1]);
      }
      set_direction(fd, 0, ComputeSpeed(dx, dy, &time)); // FIXME
    }
    // <-- Movement logic

    // Save logic -->
    if (IsKeyPressed(KEY_S))
    {
      FILE *f;
      f = fopen("latest.rods", "w");
      if (f == NULL)
      {
        // Error, as expected.
        perror("Error opening file");
        exit(-1);
      }
      SaveRods(rods, nb_rods, f);
      fclose(f);
    }
    // <-- Save logic

    // Draw rods
    DrawRods(rods, nb_rods);
    DrawFPS(0, 0);

    EndDrawing();
  } // <-- Main loop

  CloseWindow();

  return 0;
}

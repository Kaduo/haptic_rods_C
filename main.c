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
#include <getopt.h>
#include <ctype.h>

const int NB_RODS_MENU = 10;
const int UNIT_ROD_LENGTH = 40;
const int ROD_HEIGHT = 40;
const int SELECTION_COUNTDOWN = 3;
const Color COLORS[] = {LIGHTGRAY, RED, GREEN, PURPLE, YELLOW,
                        DARKGREEN, BLACK, BROWN, BLUE, ORANGE};

const int TABLET_LENGTH = 1000;
const int TABLED_HEIGHT = 600;

const double PARAMETER_NOT_SET = -10;

static const char DEFAULT_CONFIG[] = "config.cfg";
static const char DEFAULT_SPEC[] = "spec.rods";

typedef struct Rod
{
  Rectangle rect;
  int numericLength;
} Rod;

Rod NewRod(int numericLength, float x, float y)
{
  return (Rod){rect : (Rectangle){x, y, numericLength * UNIT_ROD_LENGTH, ROD_HEIGHT}, numericLength};
}

Vector2 GetTopLeft(Rod rod)
{
  return (Vector2){rod.rect.x, rod.rect.y};
}

Vector2 GetBottomRight(Rod rod)
{
  return (Vector2){rod.rect.x + rod.rect.width, rod.rect.y + rod.rect.height};
}

Vector2 GetBottomLeft(Rod rod)
{
  return (Vector2){rod.rect.x, rod.rect.y + rod.rect.height};
}

Vector2 GetTopRight(Rod rod)
{
  return (Vector2){rod.rect.x + rod.rect.width, rod.rect.y};
}

float GetTop(Rod rod)
{
  return rod.rect.y;
}

void SetTop(Rod *rod, float top)
{
  rod->rect.y = top;
}

float GetBottom(Rod rod)
{
  return rod.rect.y + rod.rect.height;
}

void SetBottom(Rod *rod, float bottom)
{
  rod->rect.y = bottom - rod->rect.height;
}

float GetLeft(Rod rod)
{
  return rod.rect.x;
}

void SetLeft(Rod *rod, float left)
{
  rod->rect.x = left;
}

float GetRight(Rod rod)
{
  return rod.rect.x + rod.rect.width;
}

void SetRight(Rod *rod, float right)
{
  rod->rect.x = right - rod->rect.width;
}

void SetTopLeft(Rod *rod, Vector2 newPos)
{
  rod->rect.x = newPos.x;
  rod->rect.y = newPos.y;
}

Color GetRodColor(Rod rod)
{
  return COLORS[rod.numericLength - 1];
}

typedef struct RodGroup
{
  int nbRods;
  Rod rods[];
} RodGroup;

typedef struct SelectionState
{
  Rod *selectedRod;
  int selectionTimer;
  Vector2 offset;
} SelectionState;

SelectionState InitSelectionState()
{
  SelectionState s;
  s.selectedRod = NULL;
  s.selectionTimer = 0;
  s.offset = (Vector2){0, 0};
  return s;
}

typedef struct AppState
{
  RodGroup *rodGroup;
  SelectionState selectionState;
} AppState;

AppState InitAppState(RodGroup *rodGroup)
{
  return (AppState){rodGroup, InitSelectionState()};
}

void SelectRodUnderMouse(SelectionState *s, RodGroup *rodGroup, Vector2 mousePosition)
{
  for (int i = 0; i < rodGroup->nbRods; i++)
  {
    Rod *rod = &(rodGroup->rods[i]);
    // If a rod is under the mouse, mark it as selected.
    if (CheckCollisionPointRec(mousePosition, rod->rect))
    {
      s->selectedRod = rod;
      s->selectionTimer = 0;
      s->offset = Vector2Subtract(GetTopLeft(*rod), mousePosition);
      break;
    }
  }
}

void ClearSelection(SelectionState *s)
{
  s->selectedRod = NULL;
  s->selectionTimer = 0;
}

void UpdateSelectionTimer(SelectionState *s)
{
  if (s->selectedRod != NULL)
  {
    s->selectionTimer += 1;
  }
}

Rod RodAfterSpeculativeMove(SelectionState s, Vector2 mousePosition)
{
  if (s.selectedRod == NULL)
  {
    fprintf(stderr, "A rod must be selected to speculate about its movement!\n");
    abort();
  }
  else
  {
    Vector2 newTopLeft = Vector2Add(mousePosition, s.offset);
    return NewRod(s.selectedRod->numericLength, newTopLeft.x, newTopLeft.y);
  }
}

enum StrictCollisionType
{
  NO_STRICT_COLLISION,
  FROM_LEFT,
  FROM_RIGHT,
  FROM_ABOVE,
  FROM_BELOW
};

enum RelativeYPosition
{
  STRICTLY_ABOVE = 0x01,
  STRICTLY_BELOW = 0x02,
  JUST_ABOVE = 0x04,
  JUST_BELOW = 0x08,
  Y_ALIGNED = 0x10
};

enum RelativeXPosition
{
  STRICTLY_LEFT = 0x01,
  STRICTLY_RIGHT = 0x02,
  JUST_LEFT = 0x04,
  JUST_RIGHT = 0x08,
  X_ALIGNED = 0x10
};

enum RelativeYPosition RelativeYPosition(Rod rod1, Rod rod2)
{
  if (GetBottom(rod1) < GetTop(rod2))
  {
    return STRICTLY_ABOVE;
  }
  else if (GetBottom(rod1) == GetTop(rod2))
  {
    return JUST_ABOVE;
  }
  else if (GetTop(rod1) > GetBottom(rod2))
  {
    return STRICTLY_BELOW;
  }
  else if (GetTop(rod1) == GetBottom(rod2))
  {
    return JUST_BELOW;
  }
  else
  {
    return Y_ALIGNED;
  }
}

enum RelativeXPosition RelativeXPosition(Rod rod1, Rod rod2)
{
  if (GetRight(rod1) < GetLeft(rod2))
  {
    return STRICTLY_LEFT;
  }
  else if (GetRight(rod1) == GetLeft(rod2))
  {
    return JUST_LEFT;
  }
  else if (GetLeft(rod1) > GetRight(rod2))
  {
    return STRICTLY_RIGHT;
  }
  else if (GetLeft(rod1) == GetRight(rod2))
  {
    return JUST_RIGHT;
  }
  else
  {
    return X_ALIGNED;
  }
}

bool StrictlyCollide(Rod rod1, Rod rod2)
{
  return (RelativeXPosition(rod1, rod2) == X_ALIGNED) && (RelativeYPosition(rod1, rod2) == Y_ALIGNED);
}

bool SoftlyCollide(Rod rod1, Rod rod2)
{
  return (RelativeXPosition(rod1, rod2) & (X_ALIGNED | JUST_RIGHT | JUST_LEFT)) && (RelativeYPosition(rod1, rod2) & (Y_ALIGNED | JUST_ABOVE | JUST_BELOW));
}

enum StrictCollisionType CheckStrictCollision(Rod rod_before, Rod rod_after, Rod other_rod)
{
  if (!StrictlyCollide(rod_after, other_rod))
  {
    return NO_STRICT_COLLISION;
  }

  switch (RelativeYPosition(rod_before, other_rod))
  {
  case JUST_ABOVE:
  case STRICTLY_ABOVE:
    return FROM_ABOVE;
  case JUST_BELOW:
  case STRICTLY_BELOW:
    return FROM_BELOW;
  default:
    break;
  }

  switch (RelativeXPosition(rod_before, other_rod))
  {
  case JUST_LEFT:
  case STRICTLY_LEFT:
    return FROM_LEFT;
  case JUST_RIGHT:
  case STRICTLY_RIGHT:
    return FROM_RIGHT;
  default:
    fprintf(stderr, "THIS HSHOLDN4T AHPPEN!\n");
    return NO_STRICT_COLLISION;
  }
}

void UpdateSelectedRodPosition(SelectionState *s, RodGroup *rodGroup, Vector2 mousePosition)
{
  if (s->selectedRod == NULL)
  {
    return;
  }
  Rod newRod = RodAfterSpeculativeMove(*s, mousePosition);

  for (int i = 0; i < rodGroup->nbRods; i++)
  {
    Rod *otherRod = &rodGroup->rods[i];
    if (s->selectedRod != otherRod)
    {
      switch (CheckStrictCollision(*(s->selectedRod), newRod, rodGroup->rods[i]))
      {
      case NO_STRICT_COLLISION:
        break;
      case FROM_ABOVE:
        SetBottom(&newRod, GetTop(*otherRod));
        break;
      case FROM_BELOW:
        SetTop(&newRod, GetBottom(*otherRod));
        break;
      case FROM_RIGHT:
        SetLeft(&newRod, GetRight(*otherRod));
        break;
      case FROM_LEFT:
        SetRight(&newRod, GetLeft(*otherRod));
        break;
      }
    }
  }

  // Another loop to insure against accidental merges.
  for (int i = 0; i < rodGroup->nbRods; i++)
  {
    Rod *otherRod = &rodGroup->rods[i];
    if (s->selectedRod != otherRod && StrictlyCollide(newRod, *otherRod))
    {
      return;
    }
  }
  s->selectedRod->rect = newRod.rect;
}

void UpdateAppState(AppState *s)
{

  Vector2 mousePosition = GetMousePosition();

  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
  {
    SelectRodUnderMouse(&s->selectionState, s->rodGroup, mousePosition);
  }
  else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
  {
    ClearSelection(&s->selectionState);
  }
  else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
  {
    UpdateSelectedRodPosition(&s->selectionState, s->rodGroup, mousePosition);
  }
  UpdateSelectionTimer(&s->selectionState);
}

typedef struct CollisionState
{

} CollisionState;

void DrawRod(Rod rod)
{
  DrawRectangleRec(rod.rect, GetRodColor(rod));
  DrawRectangleLinesEx(rod.rect, 1., BLACK);
}

void DrawRodGroup(RodGroup rodGroup[])
{
  for (int i = 0; i < rodGroup->nbRods; i++)
  {
    DrawRod(rodGroup->rods[i]);
  }
}

bool IsCollisionOnHorizontalAxis(Rectangle rect1, Rectangle rect2)
{
  return ((rect2.x < rect1.x) && (rect1.x < rect2.x + rect2.width)) ||
         ((rect2.x < rect1.x + rect1.width) &&
          (rect1.x + rect1.width < rect2.x + rect2.width));
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

void SaveRods(Rod rods[], int nbRods, FILE *file)
{
  int i;
  fprintf(file, "%d ", nbRods);
  for (i = 0; i < nbRods; i++)
  {
    fprintf(file, "%d %f %f ", rods[i].numericLength, rods[i].rect.x, rods[i].rect.y);
  }
}

RodGroup *NewRodGroup(const char *spec_name)
{
  int i;
  int nbRods;
  FILE *f;
  f = fopen(spec_name, "r");
  if (f == NULL)
  {
    perror("Couldn't open the spec: ");
  }
  fscanf(f, "%d ", &nbRods);
  RodGroup *rod_group = malloc(sizeof(RodGroup) + nbRods * sizeof(Rod));
  rod_group->nbRods = nbRods;
  for (i = 0; i < nbRods; i++)
  {
    int l;
    float x;
    float y;
    fscanf(f, "%d %f %f ", &l, &x, &y);
    rod_group->rods[i] = NewRod(l, x, y);
  }
  fclose(f);
  return rod_group;
}

int main(int argc, char **argv)
{

  // Establish connection to haptic controller
  int fd;
  fd = connect_to_tty();

  // The haptic signal won't play if no direction is set, so we set it to an arbitrary value at the start.
  set_direction(fd, 0, 10);

  // Parse command line arguments -->
  const char *config_name = DEFAULT_CONFIG;
  const char *spec_name = DEFAULT_SPEC;

  int c;
  while ((c = getopt(argc, argv, "c:s:")) != -1)
  {
    switch (c)
    {
    case 'c':
      config_name = optarg;
      break;
    case 's':
      spec_name = optarg;
      break;
    case '?':
      if (optopt == 'c')
      {
        fprintf(stderr, "L'option -%c nécessite un argument.\n", optopt);
      }
      else if (isprint(optopt))
      {
        fprintf(stderr, "L'option -%c est inconnue.\n", optopt);
      }
      else
      {
        fprintf(stderr, "Caractère inconnu : '\\x%x'.\n", optopt);
      }
      return 1;
    default:
      abort();
    }
  } // <-- Parse command line arguments

  // Load config -->
  bool config_error = false;
  config_t cfg = LoadConfig(&config_error, config_name);
  if (config_error)
  {
    return (EXIT_FAILURE);
  }
  // <-- Load config

  // Create rods -->
  RodGroup *rod_group = NewRodGroup(spec_name);
  Rod *rods = rod_group->rods;
  int nbRods = rod_group->nbRods;
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

  AppState appState = InitAppState(rod_group);

  InitWindow(TABLET_LENGTH, TABLED_HEIGHT, "HapticRods");

  // int display = GetCurrentMonitor();
  // printf("\n MonitorWidth: %d, MonitorHeight: %d", GetMonitorWidth(display), GetMonitorHeight(display));
  // InitWindow(300, 300, "HapticRods");

#ifdef FULLSCREEN
  ToggleFullscreen();
#endif
  SetTargetFPS(40);

  // Main loop
  while (!WindowShouldClose())
  {
    BeginDrawing();
    ClearBackground(RAYWHITE);

    UpdateAppState(&appState);
    DrawRodGroup(appState.rodGroup);

    // Vector2 mousePosition = GetMousePosition();

    // collided = false;

    // // Selection logic -->
    // if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    // {
    //   /* Right after selecting a rod, there is a short period during
    //   which its signal is guaranteed to play, even if there is a collision.*/
    //   signalMustPlayFrameCount = 3;

    //   // For each rod, check if it's under the mouse.
    //   for (int i = 0; i < nbRods; i++)
    //   {
    //     // If a rod is under the mouse, mark it as selected.
    //     if (CheckCollisionPointRec(mousePosition, rods[i].rect))
    //     {
    //       selected = i;

    //       // Record the distance between the rod's origin and the mouse pointer.
    //       // As long as the rod is selected, this distance will be kept constant
    //       // on the axes along which the rod is free to move.
    //       pointerOffsetX = rods[i].rect.x - mousePosition.x;
    //       pointerOffsetY = rods[i].rect.y - mousePosition.y;

    //       set_signal(fd, -1, -1, signals[rods[i].numericLength - 1]);
    //       break;
    //     }
    //   }
    // }

    // if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    // {
    //   selected = -1;
    //   clear_signal(fd);
    //   collision_frame_count = 0;
    // }
    // // <-- Selection logic

    // // Movement logic ->>

    // if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && selected >= 0)
    // {

    //   // Record the pointer's displacement to compute its speed.
    //   // Its speed is then used to compute the signal.
    //   float dx = mousePosition.x + pointerOffsetX - rods[selected].rect.x;
    //   float dy = mousePosition.y + pointerOffsetY - rods[selected].rect.y;

    //   // Record the current position of the selected rectangle,
    //   // so that it may be used to undo the move in case of an accidental merging.
    //   float oldX = rods[selected].rect.x;
    //   float oldY = rods[selected].rect.y;

    //   // TODO: cleanup: rename rect1 (selectedRect ?), and test if we can use it everywhere
    //   // instead of the verbose rods[selected].rect.
    //   // Furthermore, see if we can do the same with the rod itself,
    //   // something like selectedRod = rods[selected].
    //   Rectangle rect1 = rods[selected].rect;

    //   // Move the selected rod so that it sticks to the pointer.
    //   rods[selected].rect.x = mousePosition.x + pointerOffsetX;
    //   rods[selected].rect.y = mousePosition.y + pointerOffsetY;

    //   // For each rod, check if it is colliding with the selected rod.
    //   int i;
    //   for (i = 0; i < nbRods; i++)
    //   {

    //     Rectangle rect2 = rods[i].rect;

    //     if (CheckCollisionRecs(rods[selected].rect, rods[i].rect) &&
    //         i != selected)
    //     {

    //       collided = true;

    //       if (IsCollisionOnHorizontalAxis(rect1, rect2) ||
    //           IsCollisionOnHorizontalAxis(rect2, rect1))
    //       {

    //         if (rect1.y < rect2.y) // The selected rod is colliding from above.
    //         {
    //           rods[selected].rect.y = rect2.y - ROD_HEIGHT;
    //         }
    //         else if (rect1.y > rect2.y) // The selected rod is colliding from below.
    //         {
    //           rods[selected].rect.y = rect2.y + ROD_HEIGHT;
    //         }
    //       }
    //       else
    //       {
    //         if (rect1.x < rect2.x) { // The selected rod is colliding from the left.
    //           rods[selected].rect.x = rect2.x - rect1.width;
    //         }
    //         else if (rect1.x  > rect2.x) // The selected rod is colliding from the right.
    //         {
    //           rods[selected].rect.x = rect2.x + rect2.width;
    //         }
    //       }
    //     }

    //     // Decrement the counter that prevents the signal from being played, even in the case of a collision.
    //     if (signalMustPlayFrameCount > 0)
    //     {
    //       signalMustPlayFrameCount -= 1;
    //     }

    //     // If a new collision has just occurred,
    //     else if (collision_frame_count == 0 && newlyCollided && collided)
    //     {
    //       // TODO: sig should be defined back when the rod is selected.
    //       Signal sig = signals[rods[selected].numericLength - 1];

    //       collision_frame_count = 2;
    //       sig.offset = 255;
    //       set_signal(fd, -1, -1, sig);
    //     }
    //   }

    //   if (collided)
    //   {
    //     // Check that we didn't merge two rods by accident.
    //     for (i = 0; i < nbRods; i++)
    //     {

    //       if (CheckCollisionRecs(rods[selected].rect, rods[i].rect) &&
    //           i != selected)
    //       {
    //         rods[selected].rect.x = oldX;
    //         rods[selected].rect.y = oldY;
    //         break;
    //       }
    //     }
    //   }

    //   newlyCollided = !collided;

    //   if (collision_frame_count > 0)
    //   {
    //     collision_frame_count -= 1;
    //     if (collision_frame_count == 0)
    //     {
    //       originalSignal = false;
    //       clear_signal(fd);
    //     }
    //   }
    //   else if (!collided && !originalSignal)
    //   {
    //     originalSignal = true;

    //     // TODO: clean this up. There should be a helper function,
    //     //  something like playSelectionSignal?
    //     set_signal(fd, -1, -1, signals[rods[selected].numericLength - 1]);
    //   }
    //   set_direction(fd, 0, ComputeSpeed(dx, dy, &time)); // FIXME
    // }
    // // <-- Movement logic

    // // Save logic -->
    // if (IsKeyPressed(KEY_S))
    // {
    //   FILE *f;
    //   f = fopen("latest.rods", "w");
    //   if (f == NULL)
    //   {
    //     // Error, as expected.
    //     perror("Error opening file");
    //     exit(-1);
    //   }
    //   SaveRods(rods, nbRods, f);
    //   fclose(f);
    // }
    // // <-- Save logic

    // // Draw rods
    // DrawRodGroup(rod_group);
    // DrawFPS(0, 0);

    EndDrawing();
  } // <-- Main loop

  CloseWindow();

  return 0;
}

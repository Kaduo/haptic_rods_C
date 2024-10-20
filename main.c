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

#define NB_RODS_MENU 10
const int UNIT_ROD_LENGTH = 40;
const int ROD_HEIGHT = 40;
const Color COLORS[] = {LIGHTGRAY, RED, GREEN, PURPLE, YELLOW,
                        DARKGREEN, BLACK, BROWN, BLUE, ORANGE};

const int TABLET_LENGTH = 1000;
const int TABLED_HEIGHT = 600;

const double PARAMETER_NOT_SET = -10;

static const char DEFAULT_CONFIG[] = "config.cfg";
static const char DEFAULT_SPEC[] = "spec.rods";


const int SIGNAL_MUST_PLAY_PERIOD = 3;
const int IMPULSE_DURATION = 2;

const Signal IMPULSE_SIGNAL = (Signal){
  STEADY,
  255,
  255,
  0,
  0,
  0,
};

int ComputeSpeedV(Vector2 deltaPos, float deltaT)
{
  float speedf = abs(Vector2Length(deltaPos) /deltaT);
  int speed = floor(speedf);
  return speed;
}

int ComputeAngleV(Vector2 deltaPos) {
  return Vector2Angle((Vector2){1, 0}, deltaPos);
}


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
  return (SelectionState){selectedRod: NULL, selectionTimer: 0, offset: (Vector2){0, 0}};
}

typedef struct CollisionState {
  int collisionTimer;
  bool collided;
  bool collidedPreviously;
} CollisionState;

CollisionState InitCollisionState() {
  return (CollisionState){collisionTimer: 0, collided: false, collidedPreviously: false};
}

typedef struct TimeAndPlace {
  Vector2 mousePosition;
  Vector2 mouseDelta;
  float time;
  float deltaTime;
  int speed;
  int angle;
} TimeAndPlace;


enum SignalPlaying{NO_SIGNAL, IMPULSE, SELECTED_ROD_SIGNAL};

typedef struct SignalState  {
  enum SignalPlaying signalPlaying;
  Signal signals[NB_RODS_MENU];
  int fd;
} SignalState;

SignalState InitSignalState(config_t cfg) {
  Signal signals[NB_RODS_MENU];
  InitSignals(cfg, signals);
  SignalState signalState = (SignalState){NO_SIGNAL, connect_to_tty(), signals};
  if (signalState.fd != -1) {
      // The haptic signal won't play if no direction is set, so we set it to an arbitrary value at the start.
    set_direction(signalState.fd, 0, 10);
  }
  return signalState;
}

void ClearSignal(SignalState *sigs) {
  sigs->signalPlaying = NO_SIGNAL;
  clear_signal(sigs->fd);
}

Signal GetRodSignal(SignalState sigs, Rod rod) {
  return sigs.signals[rod.numericLength -1];
}

void SetSelectedRodSignal(SignalState *sigs, SelectionState secs, TimeAndPlace tap) {
  sigs->signalPlaying = SELECTED_ROD_SIGNAL;
  set_signal(sigs->fd, tap.angle, -1, GetRodSignal(*sigs, *secs.selectedRod));
}

void PlayImpulse(SignalState *sigs) {
  sigs->signalPlaying = IMPULSE;
  set_signal(sigs->fd, -1, -1, IMPULSE_SIGNAL);
}

void UpdateSignalState(SignalState *sigs, SelectionState secs, CollisionState cols, TimeAndPlace tap) {
  if (secs.selectedRod == NULL) {
    ClearSignal(sigs);
  }
  else if (!cols.collided && sigs->signalPlaying == NO_SIGNAL) {
    SetSelectedRodSignal(sigs, secs, tap);
  }
  else if (cols.collided) {
    if (sigs->signalPlaying == NO_SIGNAL) {
      if (secs.selectionTimer <= SIGNAL_MUST_PLAY_PERIOD) {
        SetSelectedRodSignal(sigs, secs, tap);
      }
    } else if (sigs->signalPlaying == IMPULSE && cols.collisionTimer > SIGNAL_MUST_PLAY_PERIOD + IMPULSE_DURATION) {
        ClearSignal(sigs);
    } else if (sigs->signalPlaying == SELECTED_ROD_SIGNAL && secs.selectionTimer > SIGNAL_MUST_PLAY_PERIOD) {
      PlayImpulse(sigs);
    }
  }
}


TimeAndPlace InitTimeAndPlace() {
  return (TimeAndPlace){GetMousePosition(), GetMouseDelta(), GetTime(), GetFrameTime(), 0, 0};
}

void UpdateTimeAndPlace(TimeAndPlace *tap) {
  tap->mousePosition = GetMousePosition();
  tap->mouseDelta = GetMouseDelta();
  tap->time = GetTime();
  tap->deltaTime = GetFrameTime();
  tap->angle = ComputeAngleV(tap->mouseDelta);
  if (tap->deltaTime > 0) {
    tap->speed = ComputeSpeedV(tap->mouseDelta, tap->deltaTime);
  }
}

typedef struct AppState
{
  TimeAndPlace timeAndPlace;
  RodGroup *rodGroup;
  SelectionState selectionState;
  CollisionState collisionState;
  SignalState signalState;
} AppState;

AppState InitAppState(config_t cfg, const char *specName)
{
  return (AppState){InitTimeAndPlace(), NewRodGroup(specName), InitSelectionState(), InitCollisionState(), InitSignalState(cfg)};
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

void UpdateCollisionTimer(CollisionState *s) {
  if (s->collidedPreviously) {
    s->collisionTimer += 1;
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
  case JUST_ABOVE: case STRICTLY_ABOVE:
    return FROM_ABOVE;
  case JUST_BELOW: case STRICTLY_BELOW:
    return FROM_BELOW;
  default:
    break;
  }

  switch (RelativeXPosition(rod_before, other_rod))
  {
  case JUST_LEFT: case STRICTLY_LEFT:
    return FROM_LEFT;
  case JUST_RIGHT: case STRICTLY_RIGHT:
    return FROM_RIGHT;
  default:
    fprintf(stderr, "THIS HSHOLDN4T AHPPEN!\n");
    return NO_STRICT_COLLISION;
  }
}

void ClearCollisionState(CollisionState *cs) {
  cs->collided = false;
  cs->collidedPreviously = false;
  cs->collisionTimer = 0;
}

void RegisterCollision(CollisionState *cs) {
  cs->collided = true;
}

void UpdateCollisionState(CollisionState *cs) {
  if (cs->collided) {
    cs->collisionTimer += 1;
  } else {
    cs->collisionTimer = 0;
  }
  cs->collidedPreviously = cs->collided;
  cs->collided = false;
}

void UpdateSelectedRodPosition(SelectionState *ss, CollisionState *cs, RodGroup *rodGroup, Vector2 mousePosition)
{
  if (ss->selectedRod == NULL)
  {
    return;
  }
  Rod newRod = RodAfterSpeculativeMove(*ss, mousePosition);

  for (int i = 0; i < rodGroup->nbRods; i++)
  {
    Rod *otherRod = &rodGroup->rods[i];
    if (ss->selectedRod != otherRod)
    {
      enum StrictCollisionType collisionType = CheckStrictCollision(*(ss->selectedRod), newRod, rodGroup->rods[i]);
      if (collisionType != NO_STRICT_COLLISION) {
        RegisterCollision(cs);
        switch (collisionType)
        {
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
        default:
          fprintf(stderr, "SHOULDN'T HAPPEN!!");
          abort();
        }
      }
    }
  }

  // If two rods would get merged by the move, do nothing.
  for (int i = 0; i < rodGroup->nbRods; i++)
  {
    Rod *otherRod = &rodGroup->rods[i];
    if (ss->selectedRod != otherRod && StrictlyCollide(newRod, *otherRod))
    {
      return;
    }
  }
  ss->selectedRod->rect = newRod.rect;
}

void UpdateAppState(AppState *s)
{
  
  UpdateTimeAndPlace(&s->timeAndPlace);

  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
  {
    SelectRodUnderMouse(&s->selectionState, s->rodGroup, s->timeAndPlace.mousePosition);
  }
  else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
  {
    ClearSelection(&s->selectionState);
    ClearCollisionState(&s->collisionState);
    ClearSignal(&s->signalState);
  }
  else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
  {
    UpdateSelectedRodPosition(&s->selectionState, &s->collisionState, s->rodGroup, s->timeAndPlace.mousePosition);
  }
  UpdateSelectionTimer(&s->selectionState);
}

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

  AppState appState = InitAppState(cfg, spec_name);

  InitWindow(TABLET_LENGTH, TABLED_HEIGHT, "HapticRods");

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
    DrawFPS(0, 0);

    EndDrawing();
  } // <-- Main loop

  CloseWindow();

  return 0;
}

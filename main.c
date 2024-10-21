#include "raylib.h"
#include "raymath.h"
#define TERMINAL "/dev/ttyUSB0"

#include "config.h"
#include "signals.h"
#include "rods.h"
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

const int TABLET_LENGTH = 1000;
const int TABLED_HEIGHT = 600;

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
  Signal *signals;
  int fd;
} SignalState;

SignalState InitSignalState(config_t cfg) {
  Signal signals[NB_RODS_MENU];
  InitSignals(cfg, signals);
  SignalState signalState = (SignalState){signalPlaying: NO_SIGNAL, signals: signals, fd: connect_to_tty()};
  if (signalState.fd != -1) {
      // The haptic signal won't play if no direction is set, so we set it to an arbitrary value at the start.
    set_direction(signalState.fd, 0, 10);
  }
  return signalState;
}

void ClearSignal(SignalState *sigs) {
  sigs->signalPlaying = NO_SIGNAL;
  if (sigs->fd != -1) {
    clear_signal(sigs->fd);
  }
  printf("Now playing : no signal.\n");
}

Signal GetRodSignal(SignalState sigs, Rod rod) {
  return sigs.signals[rod.numericLength -1];
}

void SetSelectedRodSignal(SignalState *sigs, SelectionState secs, TimeAndPlace tap) {
  sigs->signalPlaying = SELECTED_ROD_SIGNAL;
  if (sigs->fd != -1) {
      set_signal(sigs->fd, tap.angle, -1, GetRodSignal(*sigs, *secs.selectedRod));
  }
  printf("Now playing : the selected rod signal.\n");
}

void PlayImpulse(SignalState *sigs) {
  sigs->signalPlaying = IMPULSE;
  if (sigs->fd != -1) {
    set_signal(sigs->fd, -1, -1, IMPULSE_SIGNAL);
  }
  printf("Now playing : the impulse signal.\n");
}

void UpdateSignalState(SignalState *sigs, SelectionState secs, CollisionState cols, TimeAndPlace tap) {
  if (secs.selectedRod == NULL) {
    if (sigs->signalPlaying != NO_SIGNAL) {
        ClearSignal(sigs);
    }
    return;
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

  UpdateSignalState(&s->signalState, s->selectionState, s->collisionState, s->timeAndPlace);
  UpdateCollisionState(&s->collisionState);
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


void ParseArgs(int argc, char **argv, char **configName, char **specName) {
  int c;
  while ((c = getopt(argc, argv, "c:s:")) != -1)
  {
    switch (c)
    {
    case 'c':
      *configName = optarg;
      break;
    case 's':
      *specName = optarg;
      break;
    case '?':
      if (optopt == 'c' || optopt == 's')
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
      abort();
      break;
    default:
      abort();
    }
  }
}

int main(int argc, char **argv)
{

  // Parse command line arguments -->
  char *configName = (char *)DEFAULT_CONFIG;
  char *specName = (char *)DEFAULT_SPEC;
  ParseArgs(argc, argv, &configName, &specName);

  // Load config -->
  bool config_error = false;
  config_t cfg = LoadConfig(&config_error, configName);
  if (config_error)
  {
    return (EXIT_FAILURE);
  }

  AppState appState = InitAppState(cfg, specName);

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

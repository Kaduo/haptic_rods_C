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
#include <ws.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h> 
#include <string.h> 
#include <arpa/inet.h>

#define NB_RODS_MENU 10

const int NB_PROBLEMS = 10;

const int FPS = 40;

const int TABLET_LENGTH = 1000;
const int TABLED_HEIGHT = 600;

static const char DEFAULT_CONFIG[] = "config.cfg";
static const char DEFAULT_SPEC[] = "problem_set/problem0.rods";

const int SIGNAL_MUST_PLAY_PERIOD = 0;
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
  float speedf = abs(Vector2Length(deltaPos) / deltaT);
  int speed = floor(speedf);
  return speed;
}

int ComputeAngleV(Vector2 deltaPos)
{
  return Vector2Angle((Vector2){1, 0}, deltaPos);
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

typedef struct SelectionState
{
  Rod *selectedRod;
  int selectionTimer;
  Vector2 offset;
} SelectionState;

SelectionState InitSelectionState()
{
  return (SelectionState){selectedRod : NULL, selectionTimer : 0, offset : (Vector2){0, 0}};
}

#define MAX_ROD_COLLIDING 22

typedef struct CollisionState
{
  int collisionTimer;
  bool collided;
  bool collidedPreviously;
} CollisionState;

CollisionState InitCollisionState()
{
  return (CollisionState){collisionTimer : 0, collided : false, collidedPreviously : false};
}

// WEBSOCKET

void onopen(ws_cli_conn_t client)
{
  char *cli, *port;
  cli = ws_getaddress(client);
  port = ws_getport(client);
#ifndef DISABLE_VERBOSE
  printf("Connection opened, addr: %s, port: %s\n", cli, port);
#endif
}

void onclose(ws_cli_conn_t client)
{
  char *cli;
  cli = ws_getaddress(client);
#ifndef DISABLE_VERBOSE
  printf("Connection closed, addr: %s\n", cli);
#endif
}

void UpdateCollisionTimer(CollisionState *s)
{
  if (s->collidedPreviously)
  {
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

void ClearCollisionState(CollisionState *cs)
{
  cs->collided = false;
  cs->collidedPreviously = false;
  cs->collisionTimer = 0;
}

void RegisterCollision(CollisionState *cs)
{
  cs->collided = true;
}

typedef struct TimeAndPlace
{
  Vector2 mousePosition;
  Vector2 mouseDelta;
  float time;
  float deltaTime;
  bool MouseButtonPressed;
  bool MouseButtonReleased;
  bool MouseButtonDown;
  uint16_t speed;
  uint8_t angle;
} TimeAndPlace;

enum SignalPlaying
{
  NO_SIGNAL,
  IMPULSE,
  SELECTED_ROD_SIGNAL
};

typedef struct SignalState
{
  enum SignalPlaying signalPlaying;
  Signal *signals;
  int fd;
} SignalState;

SignalState InitSignalState(config_t cfg)
{
  Signal *signals = InitSignals(cfg);
  SignalState signalState = (SignalState){.signalPlaying =  NO_SIGNAL, .signals =  signals, .fd =  connect_to_tty()};
  if (signalState.fd != -1)
  {
    // The haptic signal won't play if no direction is set, so we set it to an arbitrary value at the start.
    set_direction(signalState.fd, 0, 10);
  }
  return signalState;
}

void ClearSignal(SignalState *sigs)
{
  sigs->signalPlaying = NO_SIGNAL;
  if (sigs->fd != -1)
  {
    clear_signal(sigs->fd);
    play_signal(sigs->fd, 0);
  }
  printf("Now playing : no signal.\n");
}

Signal GetRodSignal(SignalState sigs, Rod rod)
{
  return sigs.signals[rod.numericLength - 1];
}

void SetSelectedRodSignal(SignalState *sigs, SelectionState secs, TimeAndPlace tap)
{
  sigs->signalPlaying = SELECTED_ROD_SIGNAL;
  if (sigs->fd != -1)
  {
    set_signal(sigs->fd, -1, -1, GetRodSignal(*sigs, *secs.selectedRod));
  }
  printf("Now playing : the selected rod signal.\n");
  PrintSignal(GetRodSignal(*sigs, *secs.selectedRod));
}

void PlayImpulse(SignalState *sigs)
{
  sigs->signalPlaying = IMPULSE;
  if (sigs->fd != -1)
  {
    set_signal(sigs->fd, -1, -1, IMPULSE_SIGNAL);
  }
  printf("Now playing : the impulse signal.\n");
}

void UpdateSignalState(SignalState *sigs, SelectionState secs, CollisionState cols, TimeAndPlace tap)
{
  if (secs.selectedRod == NULL)
  {
    if (sigs->signalPlaying != NO_SIGNAL)
    {
      ClearSignal(sigs);
    }
    return;
  }
  else
  {
    if (!cols.collided && sigs->signalPlaying != SELECTED_ROD_SIGNAL)
    {
      SetSelectedRodSignal(sigs, secs, tap);
    }
    else if (cols.collided)
    {
      if (sigs->signalPlaying == NO_SIGNAL)
      {
        if (secs.selectionTimer <= SIGNAL_MUST_PLAY_PERIOD)
        {
          SetSelectedRodSignal(sigs, secs, tap);
        }
      }
      else if (sigs->signalPlaying == IMPULSE && cols.collisionTimer > SIGNAL_MUST_PLAY_PERIOD + IMPULSE_DURATION)
      {
        ClearSignal(sigs);
      }
      else if (sigs->signalPlaying == SELECTED_ROD_SIGNAL && secs.selectionTimer > SIGNAL_MUST_PLAY_PERIOD)
      {
        PlayImpulse(sigs);
      }
    }
    if (sigs->fd != -1)
    {
      set_direction(sigs->fd, tap.angle, tap.speed);
    }
  }
}


void UpdateTimeAndPlace(TimeAndPlace *tap)
{
  tap->mousePosition = GetMousePosition();
  tap->mouseDelta = GetMouseDelta();
  tap->time = GetTime();
  tap->deltaTime = GetFrameTime();
  tap->angle = ComputeAngleV(tap->mouseDelta);
  if (tap->deltaTime > 0)
  {
    tap->speed = ComputeSpeedV(tap->mouseDelta, tap->deltaTime);
  }
  tap->MouseButtonDown = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
  tap->MouseButtonPressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
  tap->MouseButtonReleased = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

TimeAndPlace InitTimeAndPlace()
{
  TimeAndPlace tap;
  UpdateTimeAndPlace(&tap);
  return tap;
}


typedef struct AppState
{
  TimeAndPlace timeAndPlace;
  RodGroup *rodGroup;
  SelectionState selectionState;
  CollisionState collisionState;
  SignalState signalState;
  int problemId;
  bool next;
  int userId;
  bool newUser;
  FILE *currentSave;
} AppState;

static AppState appState;

void onmessage(ws_cli_conn_t client,
               const unsigned char *msg, uint64_t size, int type)
{
  switch (msg[0])
  {
  case 'n':
    appState.next = true;
    appState.problemId = strtol(&(msg[1]), NULL, 10);
    break;
  case 'u':
    appState.newUser = true;
    appState.userId = strtol(&(msg[1]), NULL, 10);
  default:
    break;
  }
  ws_sendframe_txt(client, "GOT IT");
}

void LoadAppSpec(AppState *s, char *specName)
{
  free(s->rodGroup);
  s->rodGroup = NewRodGroup(specName);
}

void CreateUserFolder(AppState *s)
{
  char folderName[50];
  snprintf(folderName, 50, "user%d", s->userId);
  mkdir(folderName, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
}

void StartProblem(AppState *s)
{
  char specName[50];
  snprintf(specName, 50, "problem_set/problem%d.rods", s->problemId);
  LoadAppSpec(s, specName);
}

void OpenSaveFile(AppState *s)
{
  char saveName[50];
  snprintf(saveName, 50, "user%d/save%d.tap", s->userId, s->problemId);
  free(s->currentSave);
  s->currentSave = fopen(saveName, "w");
  fprintf(s->currentSave, "s ");
  SaveRodGroup(s->rodGroup, s->currentSave);
  fprintf(s->currentSave, "\nr %f \n", s->timeAndPlace.time);
}

AppState InitAppState(config_t cfg, int firstUserId, int firstProblemId)
{
  AppState res = (AppState){InitTimeAndPlace(),
                            rodGroup : NULL,
                            InitSelectionState(),
                            InitCollisionState(),
                            InitSignalState(cfg),
                            problemId : firstProblemId,
                            next : false,
                            userId : firstUserId,
                            newUser : false,
                            currentSave : NULL};
  CreateUserFolder(&res);
  StartProblem(&res);
  OpenSaveFile(&res);
  return res;
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

void UpdateCollisionState(CollisionState *cs)
{
  if (cs->collided)
  {
    cs->collisionTimer += 1;
  }
  else
  {
    cs->collisionTimer = 0;
  }
  cs->collidedPreviously = cs->collided;
  cs->collided = false;
}

typedef struct Corner
{
  Vector2 coords;
  float dist;
} Corner;

int compareCorners(const void *a, const void *b)
{
  Corner corner_a = *((Corner *)a);
  Corner corner_b = *((Corner *)b);

  if (corner_a.dist < corner_b.dist)
    return 1;
  else if (corner_a.dist > corner_b.dist)
    return -1;
  else
    return 0;
}

typedef struct Bound
{
  float value;
  enum StrictCollisionType collisionType;
} Bound;

Bound newBound(Rod boundingRod, enum StrictCollisionType collisionType)
{
  float value;
  switch (collisionType)
  {
  case FROM_ABOVE:
    value = GetTop(boundingRod);
    break;
  case FROM_BELOW:
    value = GetBottom(boundingRod);
    break;

  case FROM_RIGHT:
    value = GetRight(boundingRod);
    break;

  case FROM_LEFT:
    value = GetLeft(boundingRod);
    break;

  default:
    fprintf(stderr, "SHOULDN'T HAPPEN!!\n ONLY CALL THIS FUNCTION WHEN THERE IS A COLLISION !\n");
    abort();
  }
  return (Bound){value, collisionType};
}

void UpdateSelectedRodPosition2(SelectionState *ss, CollisionState *cs, RodGroup *rodGroup, TimeAndPlace tap)
{
  if (ss->selectedRod == NULL)
  {
    return;
  }

  Rod targetRod = RodAfterSpeculativeMove(*ss, tap.mousePosition);

  Bound yBounds[22];
  int nbYBounds = 0;

  yBounds[0] = newBound(*(ss->selectedRod), FROM_RIGHT);
  Bound xBounds[22];
  int nbXBounds = 0;

  for (int i = 0; i < rodGroup->nbRods; i++)
  {
    Rod *otherRod = &rodGroup->rods[i];
    if (ss->selectedRod != otherRod)
    {
      StrictCollisionType collisionType = CheckStrictCollision(*(ss->selectedRod), targetRod, *otherRod);
      if (collisionType != NO_STRICT_COLLISION)
      {
        RegisterCollision(cs);

        if (collisionType == FROM_ABOVE || collisionType == FROM_BELOW)
        {
          yBounds[nbYBounds] = newBound(*otherRod, collisionType);
          nbYBounds += 1;
        }
        else
        {

          xBounds[nbXBounds] = newBound(*otherRod, collisionType);
          nbXBounds += 1;
        }
      }
    }
  }

  if (!cs->collided)
  {
    *ss->selectedRod = targetRod;
    return;
  }

  yBounds[nbYBounds] = (Bound){.value =  GetBottom(targetRod), FROM_ABOVE};
  nbYBounds += 1;

  xBounds[nbXBounds] = (Bound){.value =  GetRight(targetRod), FROM_LEFT};
  nbXBounds += 1;

  yBounds[nbYBounds] = (Bound){.value =  GetBottom(*(ss->selectedRod)), FROM_ABOVE};
  nbYBounds += 1;

  xBounds[nbXBounds] = (Bound){.value =  GetRight(*(ss->selectedRod)), FROM_LEFT};
  nbXBounds += 1;

  Rod candidateRod = *(ss->selectedRod);
  Rod bestRod = *(ss->selectedRod);
  float bestDist = Vector2DistanceSqr(GetTopLeft(targetRod), GetTopLeft(candidateRod));
  for (int ix = 0; ix < nbXBounds; ix++)
  {
    for (int iy = 0; iy < nbYBounds; iy++)
    {
      if (yBounds[iy].collisionType == FROM_ABOVE)
      {
        SetBottom(&candidateRod, yBounds[iy].value);
      }
      else
      {
        SetTop(&candidateRod, yBounds[iy].value);
      }

      if (xBounds[ix].collisionType == FROM_LEFT)
      {
        SetRight(&candidateRod, xBounds[ix].value);
      }
      else
      {
        SetLeft(&candidateRod, xBounds[ix].value);
      }

      float candidateDist = Vector2DistanceSqr(GetTopLeft(targetRod), GetTopLeft(candidateRod));

      if (candidateDist < bestDist)
      {
        bool noCollision = true;
        for (int i = 0; i < rodGroup->nbRods; i++)
        {
          if (&rodGroup->rods[i] != ss->selectedRod && StrictlyCollide(rodGroup->rods[i], candidateRod))
          {
            noCollision = false;
            break;
          }
        }
        if (noCollision)
        {
          bestDist = candidateDist;
          bestRod = candidateRod;
        }
      }
    }
  }
  SetTopLeft(ss->selectedRod, GetTopLeft(bestRod));
}

void ClearAppState(AppState *s)
{
  ClearCollisionState(&s->collisionState);
  ClearSelection(&s->selectionState);
  ClearSignal(&s->signalState);
  if (s->currentSave != NULL) {
    fclose(appState.currentSave);
  }
  s->next = false;
  s->newUser = false;
}

typedef enum MouseState
{
  RELEASED,
  PRESSED,
  DOWN,
} MouseState;

void SaveTap(AppState *s)
{
  if (s->timeAndPlace.MouseButtonReleased)
  {
    fprintf(s->currentSave, "r %f \n\n", s->timeAndPlace.time);
  }
  else
  {
    fprintf(s->currentSave,
            "%f %f %f \n",
            s->timeAndPlace.time,
            s->timeAndPlace.mousePosition.x,
            s->timeAndPlace.mousePosition.y);
  }
}

void UpdateTapFromSave(TimeAndPlace *tap, FILE *saveFile)
{
  char *line;
  size_t len = 0;
  ssize_t read;
  while ((read = getline(&line, &len, saveFile)) != -1)
  {
    if (line[0] != '\n' && line[0] != 'r' && line[0] != 's') {
      float newTime;
      float newMouseX;
      float newMouseY;
      sscanf(line, "%f %f %f ", &newTime, &newMouseX, &newMouseY);
      Vector2 newMousePos = (Vector2){newMouseX, newMouseY};

      if (tap->MouseButtonReleased) {
        tap->MouseButtonPressed = true;
        tap->MouseButtonReleased = false;
        WaitTime(newTime - tap->time - 1./FPS);
      } else {
        tap->MouseButtonPressed = false;
        tap->MouseButtonDown = true;
      }
      tap->time = newTime;
      tap->mousePosition = newMousePos;
      return;

    } else if (line[0] == 'r')
    {
      tap->MouseButtonReleased = true;
      tap->MouseButtonDown = false;
      tap->MouseButtonPressed = false;
      float newTime;
      sscanf(line, "r %f", &newTime);
      tap->time = newTime;
      return;
    }
  }
  printf("GOODBYE\n");
  CloseWindow();
}

void UpdateAppState(AppState *s, FILE *tapReplay)
{
  if (tapReplay != NULL) {
    UpdateTapFromSave(&s->timeAndPlace, tapReplay);
  } else {
    UpdateTimeAndPlace(&s->timeAndPlace);
  }

  bool somethingGoingOn = true;
  if (s->timeAndPlace.MouseButtonPressed)
  {
    SelectRodUnderMouse(&s->selectionState, s->rodGroup, s->timeAndPlace.mousePosition);
  }
  else if (s->timeAndPlace.MouseButtonReleased)
  {
    ClearSelection(&s->selectionState);
    ClearCollisionState(&s->collisionState);
    ClearSignal(&s->signalState);
  }
  else if (s->timeAndPlace.MouseButtonDown)
  {
    UpdateSelectedRodPosition2(&s->selectionState, &s->collisionState, s->rodGroup, s->timeAndPlace);
  } else {
    somethingGoingOn = false;
  }

  if (somethingGoingOn && tapReplay == NULL) {
    SaveTap(s);
  }
  

  UpdateSignalState(&s->signalState, s->selectionState, s->collisionState, s->timeAndPlace);
  UpdateCollisionState(&s->collisionState);
  UpdateSelectionTimer(&s->selectionState);

  if (s->problemId > NB_PROBLEMS)
  {
    fclose(s->currentSave);
    return;
  }

  if (IsKeyPressed(KEY_N) || s->next)
  {
    ClearAppState(s);
    StartProblem(s);
    OpenSaveFile(s);
  }

  if (IsKeyPressed(KEY_U) || s->newUser)
  {
    ClearAppState(s);
    CreateUserFolder(s);
    StartProblem(s);
    OpenSaveFile(s);
  }
}
void ParseArgs(int argc, char **argv, char **configName, char **specName, char **replayName)
{
  int c;
  while ((c = getopt(argc, argv, "c:s:r:")) != -1)
  {
    switch (c)
    {
    case 'c':
      *configName = optarg;
      break;
    case 's':
      *specName = optarg;
      break;
    case 'r':
      *replayName = optarg;
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

  const char *host;

  #ifdef DESKTOP
  host = "localhost";
  #else
  host = "192.168.1.24";
  #endif
  // Create a websocket
  ws_socket(&(struct ws_server){
      /*ma
       * Bind host:
       * localhost -> localhost/127.0.0.1
       * 0.0.0.0   -> global IPv4
       * ::        -> global IPv4+IPv6 (DualStack)
       */
      .host = host,
      .port = 8080,
      .thread_loop = 1,
      .timeout_ms = 1000,
      .evs.onopen = &onopen,
      .evs.onclose = &onclose,
      .evs.onmessage = &onmessage});

  SetTraceLogLevel(LOG_ERROR);

  // Parse command line arguments -->
  char *configName = (char *)DEFAULT_CONFIG;
  char *specName = (char *)DEFAULT_SPEC;
  char *replayName = NULL;
  ParseArgs(argc, argv, &configName, &specName, &replayName);

  // Load config -->
  bool config_error = false;
  config_t cfg = LoadConfig(&config_error, configName);
  printf("CONFIG : %s\n", configName);
  if (config_error)
  {
    return (EXIT_FAILURE);
  }

  appState = InitAppState(cfg, 0, 10);

  InitWindow(TABLET_LENGTH, TABLED_HEIGHT, "HapticRods");

#ifndef DESKTOP
  HideCursor();
  ToggleFullscreen();
#endif

  SetTargetFPS(FPS);

  FILE *save = NULL;
  if (replayName != NULL) {
    save = fopen(replayName, "r");
    printf("%p\n", save);
  }

  // Main loop
  while (!WindowShouldClose())
  {
    BeginDrawing();
    ClearBackground(RAYWHITE);


    UpdateAppState(&appState, save);
    DrawRodGroup(appState.rodGroup);

    if (save != NULL && (appState.timeAndPlace.MouseButtonDown || appState.timeAndPlace.MouseButtonPressed)) {
      DrawCircle(appState.timeAndPlace.mousePosition.x, 
          appState.timeAndPlace.mousePosition.y,
          5,
          RED);
    }

    DrawFPS(0, 0);

    EndDrawing();
  } // <-- Main loop

  if (appState.currentSave != NULL) {
    fclose(appState.currentSave);
  }
  CloseWindow();

    // TODO : find local address automatically
    //   struct ifaddrs * ifAddrStruct=NULL;
    // struct ifaddrs * ifa=NULL;
    // void * tmpAddrPtr=NULL;

    // getifaddrs(&ifAddrStruct);

    // for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
    //     if (!ifa->ifa_addr) {
    //         continue;
    //     }
    //     if (ifa->ifa_addr->sa_family == AF_INET) { // check it is IP4
    //         // is a valid IP4 Address
    //         tmpAddrPtr=&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
    //         char addressBuffer[INET_ADDRSTRLEN];
    //         inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
    //         if (strcmp(ifa->ifa_name, "wlan0") == 0) {
    //           printf("My local address is : %s\n", addressBuffer);
    //         }
    //         break;
    //     }
    // }
    // if (ifAddrStruct!=NULL) freeifaddrs(ifAddrStruct);

  return 0;
}

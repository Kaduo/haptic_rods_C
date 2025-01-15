#include "signals.h"
#include "tinyexpr.h"
#include "config.h"
#include <stdio.h>
#include <stddef.h>
#include <libconfig.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

// For each rod_length (1 through 10)
// For each signal parameter (kind, period, duty...)
// Get the corresponding config key
// Get the corresponding pointer
// Find which parameter level is set, in order of precedence (per rod > per group > global > default)
// Translate the config string into a value (either evaluate the expr or use a mapping (in the case of signal kind))
// Assign the value to the signal

const int nbRodLengths = 10;
const int nbParameters = 5;
const char *lengthSettingNames[] = {"r1", "r2", "r3", "r4", "r5",
                         "r6", "r7", "r8", "r9", "r10"};
const char *groupSettingNames[] = {"g1-7", "g2-4-8", "g3-6-9", "g2-4-8", "g5-10",
                  "g3-6-9", "g1-7", "g2-4-8", "g3-6-9", "g5-10"};



// typedef int(*EvalFunc)(char*);

// EvalFunc EvalFuncFactory() {
//   EvalFunc deleteme;
//   int hop() {
//     return 23;
//   }
// }

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


char *ReadStringFromSetting(config_t *cfg, config_setting_t *setting, char *key) {
  const char *string_expr;
  return config_setting_lookup_string(setting, key, &string_expr);
}

void BigFunction(config_t cfg) {
  Signal *signals = malloc(nbRodLengths * sizeof(Signal));
  for (int l =0; l<nbRodLengths; l++) { // Loop over rod lengths
    signals[l] = signal_new(SINE, 0, 0, 0, 0, 0); // TODO : initialize with better default values maybe? At least make it explicit that these are the default.
    config_setting_t *lengthSetting = config_lookup(&cfg, lengthSettingNames[l]);
    config_setting_t *groupSetting = config_lookup(&cfg, groupSettingNames[l]);
    config_setting_t *globalSetting = NULL;
    for (int p = 0; p < nbParameters; p++) { // Loop over signal parameters
      // Test length settings first
      if (lengthSetting != NULL) {

      }
      // Then group settings
      else if (groupSetting != NULL) {

      }
      // Then global
    }
  }
}

// char *getConfigValue(config_setting_t *setting, ) {

// }

typedef struct Parameter
{
  char *key;
  void *pointerToValue;
  int nbBytes;
} Parameter;

void *GetParameterPointer(Signal *signal, char *parameterKey)
{
  const int nKeys = 5;
  struct KeyPointerPair
  {
    char *key;
    void *value;
    int nbBytes;
  };
  struct KeyPointerPair keyValuePairs[5] = {{"signal_type", &signal->signal_type, 1},
                                            {"offset", &signal->offset, 1},
                                            {"period", &signal->period, 2},
                                            {"duty", &signal->duty, 1},
                                            {"amplitude", &signal->amplitude, 1}};

  for (int i = 0; i < nKeys; i++)
  {
    if (strcmp(keyValuePairs[i].key, parameterKey) == 0)
    {
      return keyValuePairs[i].value;
    }
  }
  return NULL;
}

const double PARAMETER_NOT_SET = -10;

typedef struct KeyValuePair
{
  char *key;
  int value;
} KeyValuePair;

KeyValuePair signalKeyMap[5] = {{"sine", SINE},
                                {"steady", STEADY},
                                {"triangle", TRIANGLE},
                                {"front teeth", FRONT_TEETH},
                                {"back teeth", BACK_TEETH}};

KeyValuePair newPair(char *key, int value)
{
  return (KeyValuePair){
      .key = key,
      .value = value};
}

typedef struct ParameterDict
{
  int length;
  KeyValuePair entries[];
} ParameterDict;

int get(ParameterDict *dict, char *key)
{
  printf("key : %s", key);
  for (int i = 0; i < dict->length; i++)
  {
    printf("compare to : %s\n", dict->entries[i].key);
    if (strcmp(key, dict->entries[i].key) == 0)
    {
      return dict->entries[i].value;
    }
  }
  fprintf(stderr, "Erreur : la clé %s n'est pas dans le dictionnaire.\n", key);
  return 9999;
}

ParameterDict *CreateDict()
{
  int length = 5;
  ParameterDict *dict = malloc(sizeof(ParameterDict) + length * sizeof(KeyValuePair));
  dict->length = length;
  dict->entries[0] = newPair("sine", SINE);
  dict->entries[1] = newPair("steady", STEADY);
  dict->entries[2] = newPair("triangle", TRIANGLE);
  dict->entries[3] = newPair("front teeth", FRONT_TEETH);
  dict->entries[4] = newPair("back teeth", BACK_TEETH);
  return dict;
}

void SetSignalKind(config_t *cfg, SignalType *signalKind)
{
  const char *signalName;
  ParameterDict *dict = CreateDict();
  if (config_lookup_string(cfg, "signal_type", &signalName))
  {
    *signalKind = get(dict, signalName);
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

Signal *InitSignals(config_t cfg)
{

  Signal *signals = malloc(10 * sizeof(Signal));
  char *signal_parameter_name = "signal_type";
  SignalType signal = SINE;
  SetSignalKind(&cfg, &signal);

  int i;
  for (i = 0; i < 10; i++)
  {
    int l = i + 1;
    signals[i] = signal_new(signal, 0, 0, 0, 0, 0);
    SetExpr16ParameterOfSignal(
        &cfg, &signals[i].period, l,
        "period_expr", 0xFFFF);
    SetExpr8ParameterOfSignal(
        &cfg, &signals[i].amplitude,
        l, "amplitude_expr", 0xFF);
    SetExpr8ParameterOfSignal(
        &cfg, &signals[i].duty, l,
        "duty_expr", 0xFF);
    SetExpr8ParameterOfSignal(
        &cfg, &signals[i].offset, l,
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
          // signals[i].amplitude = ClampDouble(amplitude, 0, 0xFF);
          void *parameterPointer = GetParameterPointer(&signals[i], "amplitude");
          printf("new pointer ! %p\n", parameterPointer);
          printf("actual pointer ?? %p\n", &signals[i].amplitude);
          printf("wtf is going on around here... %p\n", (double *)parameterPointer);
          printf("yipee, %f\n", *(double *)(parameterPointer));

          *(uint8_t *)parameterPointer = ClampDouble(amplitude, 0, 0xFF);
          printf("houlalal, %f\n", *(double *)(parameterPointer));
          printf("houlalallalalalala, %d\n", *(uint8_t *)(parameterPointer));

          printf("uhuhuh %d\n", signals[i].amplitude);
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

  return signals;
}

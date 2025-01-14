#include "signals.h"
#include "tinyexpr.h"
#include "config.h"
#include <stdio.h>
#include <stddef.h>
#include <libconfig.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

const double PARAMETER_NOT_SET = -10;

typedef struct KeyValuePair {
  char *key;
  int value;
} KeyValuePair;

typedef struct ParameterDict {
  int length;
  KeyValuePair *entries;
} ParameterDict;

int get(ParameterDict dict, char *key) {
  for (int i = 0; i < dict.length; i++) {
    if (strcmp(key, dict.entries[i].key) == 0)
    {
      return dict.entries[i].value;
    }
  }
  fprintf(stderr, "Erreur : la clé %s n'est pas dans le dictionnaire.\n", key);
  return 9999;
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

  Signal *signals = malloc(10*sizeof(Signal));
  char *signal_parameter_name = "signal_type";
  SignalType signal = SINE;
  SetSignalKind(&cfg, &signal);

  int i;
  for (i = 0; i < 10; i++)
  {
    int l = i+1;
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

  return signals;
}

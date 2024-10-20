#include <libconfig.h>
#include "signals.h"
#include <stdbool.h>

config_t LoadConfig(bool *err, const char *config_name);
void InitSignals(config_t cfg, Signal signals[]);
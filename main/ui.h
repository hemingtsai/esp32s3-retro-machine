#pragma once

#include <stdbool.h>

#include "machine.h"

void ui_execute_command(int argc, char *argv[], struct Machine *machine, struct ControlPanel *panel);

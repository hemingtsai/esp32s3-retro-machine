#include <stdlib.h>
#include <string.h>

#include "ui.h"

static const char *const fault_names[] = {
    "NONE",
    "DIV_ZERO",
};

static bool parse_number(const char *text, uint16_t *value)
{
  char buffer[16];
  size_t length = strlen(text);

  /* All console numbers are hexadecimal, with an optional h/H suffix
     matching the assembler syntax (e.g. 2000h). */

  if (length == 0 || length >= sizeof(buffer))
    return false;

  if (text[length - 1] == 'h' || text[length - 1] == 'H')
  {
    memcpy(buffer, text, length - 1);
    buffer[length - 1] = '\0';
  }
  else
  {
    memcpy(buffer, text, length + 1);
  }

  char *end;
  unsigned long parsed = strtoul(buffer, &end, 16);
  if (*end != '\0' || parsed > 0xFFFF)
    return false;
  *value = (uint16_t)parsed;
  return true;
}

static void command_read(int argc, char *argv[], struct Machine *machine)
{
  uint16_t address;

  if (argc < 2 || !parse_number(argv[1], &address))
  {
    MACHINE_LOG("usage: rd <addr> [count]");
    return;
  }

  uint16_t count = 8;
  if (argc >= 3 && !parse_number(argv[2], &count))
  {
    MACHINE_LOG("usage: rd <addr> [count]");
    return;
  }
  if (count == 0)
    count = 1;

  for (uint16_t i = 0; i < count; i++)
    MACHINE_LOG("%04X: %04X", (uint16_t)(address + i), machine->memory[address + i]);
}

static void command_write(int argc, char *argv[], struct Machine *machine)
{
  uint16_t address;

  if (argc < 3 || !parse_number(argv[1], &address))
  {
    MACHINE_LOG("usage: wr <addr> <value> [value...]");
    return;
  }

  uint16_t cursor = address;
  unsigned int written = 0;

  for (int i = 2; i < argc; i++)
  {
    uint16_t value;

    if (!parse_number(argv[i], &value))
    {
      MACHINE_LOG("bad value: %s", argv[i]);
      return;
    }

    machine->memory[cursor] = value;
    cursor = (uint16_t)(cursor + 1);
    written++;
  }

  MACHINE_LOG("%04X <= %u word(s)", address, written);
}

static void command_load(int argc, char *argv[], struct Machine *machine)
{
  uint16_t address;

  if (argc != 2 || !parse_number(argv[1], &address))
  {
    MACHINE_LOG("usage: load <addr> (hex words on stdin, '.' to finish)");
    return;
  }

  uint16_t cursor = address;
  unsigned int loaded = 0;
  bool terminated = false;
  char line[256];

  while (fgets(line, sizeof(line), stdin))
  {
    char *p = line;

    while (*p == ' ' || *p == '\t')
      p++;

    if (p[0] == '.')
    {
      terminated = true;
      break;
    }

    if (p[0] == '\n' || p[0] == '\r' || p[0] == '\0')
      continue;

    char *end = p;

    for (;;)
    {
      char *before = end;
      unsigned long value = strtoul(before, &end, 16);

      if (end == before)
        break;

      machine->memory[cursor] = (uint16_t)value;
      cursor = (uint16_t)(cursor + 1);
      loaded++;
    }
  }

  MACHINE_LOG("loaded %u word(s) at %04X%s", loaded, address, terminated ? "" : " (missing terminator)");
}

static void command_pc(int argc, struct Machine *machine, char *argv[])
{
  uint16_t address;

  if (argc != 2 || !parse_number(argv[1], &address))
  {
    MACHINE_LOG("usage: pc <addr>");
    return;
  }

  write_register(machine, PC, address);
  machine->halted = false;
  machine->fault = FAULT_NONE;
  MACHINE_LOG("PC = %04X", address);
}

void ui_execute_command(int argc, char *argv[], struct Machine *machine, struct ControlPanel *panel)
{
  if (argc < 1)
  {
    MACHINE_LOG("commands: rd wr load pc run step stop reset info");
    return;
  }

  const char *command = argv[0];

  if (strcmp(command, "rd") == 0)
    command_read(argc, argv, machine);
  else if (strcmp(command, "wr") == 0)
    command_write(argc, argv, machine);
  else if (strcmp(command, "load") == 0)
    command_load(argc, argv, machine);
  else if (strcmp(command, "pc") == 0)
    command_pc(argc, machine, argv);
  else if (strcmp(command, "run") == 0)
    panel_run(machine);
  else if (strcmp(command, "step") == 0)
    panel_step(machine);
  else if (strcmp(command, "stop") == 0)
    panel_stop(machine);
  else if (strcmp(command, "reset") == 0)
    panel_reset(machine, panel);
  else if (strcmp(command, "info") == 0)
  {
    MACHINE_LOG("PC=%04X MAR=%04X SP=%04X IR=%04X",
                read_register(machine, PC), read_register(machine, MAR),
                read_register(machine, SP), read_register(machine, IR));
    MACHINE_LOG("RS=%04X halted=%d running=%d fault=%s flags=%c%c%c%c",
                read_register(machine, RS), machine->halted, machine->running,
                fault_names[machine->fault],
                (machine->flags & FLAG_V) ? 'V' : '-',
                (machine->flags & FLAG_C) ? 'C' : '-',
                (machine->flags & FLAG_N) ? 'N' : '-',
                (machine->flags & FLAG_Z) ? 'Z' : '-');
  }
  else
    MACHINE_LOG("unknown command: %s", command);
}

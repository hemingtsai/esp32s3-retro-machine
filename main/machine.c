#include "machine.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

static void execute_alu(struct Machine *machine, const struct Instruction *instruction);

const char *const register_names[16] = {
    "ORD0", "ORD1", "PC", "IR", "MAR", "SP", "RS", "DISPLAY",
    "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7"};

const char *const opcode_names[32] = {
    "MOV", "LDI", "RED", "WRT", "PUSH", "POP", "MNXT", "MPRV",
    "ADD", "SUB", "DIV", "INC", "DEC", "AND", "OR", "XOR",
    "NOT", "SHL", "SHR", "CMP", "JMP", "JZ", "JNZ", "JN",
    "JP", "JC", "JNC", "JV", "CALL", "RET", "NOP", "HLT"};

void format_instruction(const struct Instruction *instruction, char *buffer, size_t size)
{
  const char *mnemonic = opcode_names[instruction->opcode];

  switch (instruction->type)
  {
  case R:
    snprintf(buffer, size, "%s %s,%s", mnemonic,
             register_names[instruction->r.source],
             register_names[instruction->r.destination]);
    break;
  case I:
    snprintf(buffer, size, "%s %s,%04Xh", mnemonic,
             register_names[instruction->i.destination],
             instruction->i.immediate);
    break;
  case U:
    snprintf(buffer, size, "%s %s", mnemonic,
             register_names[instruction->u.reg]);
    break;
  case J:
    snprintf(buffer, size, "%s %s", mnemonic,
             register_names[instruction->j.target]);
    break;
  default:
    snprintf(buffer, size, "%s", mnemonic);
    break;
  }
}


void reset_cpu(struct Machine *machine)
{
  memset(machine->reg, 0, sizeof(machine->reg));
  machine->flags = (enum Flag)0;
  machine->halted = false;
  machine->running = false;
  machine->fault = FAULT_NONE;
}

void reset_machine(struct Machine *machine)
{
  memset(machine->memory, 0, sizeof(machine->memory));
  reset_cpu(machine);
}

uint16_t read_register(const struct Machine *machine, enum Register reg)
{
  return machine->reg[reg];
}

void write_register(struct Machine *machine, enum Register reg, uint16_t value)
{
  machine->reg[reg] = value;
}

static uint16_t fetch(const uint16_t memory[], uint16_t pc)
{
  return memory[pc];
}

void reset_control_panel(struct ControlPanel *panel)
{
  panel->display = PANEL_PC;
  panel->input_mode = PANEL_LOW_BYTE;
  panel->switches = 0;
  panel->input_latch = 0;
}

uint16_t panel_read_display(const struct Machine *machine, const struct ControlPanel *panel)
{
  switch (panel->display)
  {
  case PANEL_PC:
    return read_register(machine, PC);
  case PANEL_MAR:
    return read_register(machine, MAR);
  case PANEL_IR:
    return read_register(machine, IR);
  case PANEL_R0:
    return read_register(machine, R0);
  case PANEL_R1:
    return read_register(machine, R1);
  case PANEL_R2:
    return read_register(machine, R2);
  case PANEL_R3:
    return read_register(machine, R3);
  case PANEL_RS:
    return read_register(machine, RS);
  case PANEL_MEMORY:
    return machine->memory[read_register(machine, MAR)];
  default:
    return 0;
  }
}

void panel_set_switches(struct ControlPanel *panel, uint8_t switches)
{
  panel->switches = switches;
}

void panel_set_input_mode(struct ControlPanel *panel, enum PanelInputMode mode)
{
  panel->input_mode = mode;
}

void panel_load(struct ControlPanel *panel)
{
  switch (panel->input_mode)
  {
  case PANEL_LOW_BYTE:
    panel->input_latch = (uint16_t)((panel->input_latch & 0xFF00) | panel->switches);
    break;
  case PANEL_HIGH_BYTE:
    panel->input_latch = (uint16_t)((panel->input_latch & 0x00FF) | ((uint16_t)panel->switches << 8));
    break;
  }
}

void panel_examine(struct ControlPanel *panel)
{
  panel->display = PANEL_MEMORY;
}

void panel_deposit(struct Machine *machine, const struct ControlPanel *panel)
{
  machine->memory[read_register(machine, MAR)] = panel->input_latch;
}

void panel_mnxt(struct Machine *machine)
{
  machine->reg[MAR]++;
}

void panel_run(struct Machine *machine)
{
  machine->running = true;
  uint32_t steps = 0;
  while (machine->running && step_machine(machine))
  {
    if (++steps >= MACHINE_RUN_STEP_LIMIT)
    {
      MACHINE_LOG("RUN aborted: step limit %lu reached", (unsigned long)steps);
      break;
    }
#ifdef ESP_PLATFORM
    if ((steps & 0xFFFF) == 0)
      vTaskDelay(1);
#endif
  }
  machine->running = false;
}

void panel_stop(struct Machine *machine)
{
  machine->running = false;
}

bool panel_step(struct Machine *machine)
{
  return step_machine(machine);
}

void panel_reset(struct Machine *machine, struct ControlPanel *panel)
{
  reset_cpu(machine);
  reset_control_panel(panel);
}

size_t decode_instruction(const uint16_t memory[], uint16_t pc, struct Instruction *instruction)
{
  uint16_t word = fetch(memory, pc);
  enum Opcode opcode = (enum Opcode)((word >> 11) & 0x1F);
  enum Register reg = (enum Register)((word >> 7) & 0x0F);

  instruction->opcode = opcode;

  switch (opcode)
  {
  case MOV:
    instruction->type = R;
    instruction->r.source = reg;
    instruction->r.destination = (enum Register)((word >> 3) & 0x0F);
    return 1;
  case LDI:
    instruction->type = I;
    instruction->i.destination = reg;
    instruction->i.immediate = fetch(memory, (uint16_t)(pc + 1));
    return 2;
  case RED:
  case WRT:
  case PUSH:
  case POP:
    instruction->type = U;
    instruction->u.reg = reg;
    return 1;
  case JMP:
  case JZ:
  case JNZ:
  case JN:
  case JP:
  case JC:
  case JNC:
  case JV:
  case CALL:
    instruction->type = J;
    instruction->j.target = reg;
    return 1;
  default:
    instruction->type = N;
    return 1;
  }
}

static void execute_data(struct Machine *machine, const struct Instruction *instruction)
{
  switch (instruction->opcode)
  {
  case MOV:
    write_register(machine, instruction->r.destination, read_register(machine, instruction->r.source));
    break;
  case LDI:
    write_register(machine, instruction->i.destination, instruction->i.immediate);
    break;
  case RED:
    write_register(machine, instruction->u.reg, machine->memory[read_register(machine, MAR)]);
    break;
  case WRT:
    machine->memory[read_register(machine, MAR)] = read_register(machine, instruction->u.reg);
    break;
  case PUSH:
    machine->memory[--machine->reg[SP]] = read_register(machine, instruction->u.reg);
    break;
  case POP:
    write_register(machine, instruction->u.reg, machine->memory[machine->reg[SP]++]);
    break;
  case MNXT:
    machine->reg[MAR]++;
    break;
  case MPRV:
    machine->reg[MAR]--;
    break;
  default:
    break;
  }
}

static bool jump_taken(const struct Machine *machine, enum Opcode opcode)
{
  switch (opcode)
  {
  case JZ:
    return machine->flags & FLAG_Z;
  case JNZ:
    return !(machine->flags & FLAG_Z);
  case JN:
    return machine->flags & FLAG_N;
  case JP:
    return !(machine->flags & FLAG_N);
  case JC:
    return machine->flags & FLAG_C;
  case JNC:
    return !(machine->flags & FLAG_C);
  case JV:
    return machine->flags & FLAG_V;
  default:
    return false;
  }
}

void execute(struct Machine *machine, const struct Instruction *instruction)
{
  switch (instruction->opcode)
  {
  case MOV:
  case LDI:
  case RED:
  case WRT:
  case PUSH:
  case POP:
  case MNXT:
  case MPRV:
    execute_data(machine, instruction);
    break;
  case ADD:
  case SUB:
  case DIV:
  case INC:
  case DEC:
  case AND:
  case OR:
  case XOR:
  case NOT:
  case SHL:
  case SHR:
  case CMP:
    execute_alu(machine, instruction);
    break;
  case JMP:
    write_register(machine, PC, read_register(machine, instruction->j.target));
    break;
  case JZ:
  case JNZ:
  case JN:
  case JP:
  case JC:
  case JNC:
  case JV:
    if (jump_taken(machine, instruction->opcode))
      write_register(machine, PC, read_register(machine, instruction->j.target));
    break;
  case CALL:
    machine->memory[--machine->reg[SP]] = read_register(machine, PC);
    write_register(machine, PC, read_register(machine, instruction->j.target));
    break;
  case NOP:
    break;
  case RET:
    write_register(machine, PC, machine->memory[read_register(machine, SP)]);
    machine->reg[SP]++;
    break;
  case HLT:
    machine->halted = true;
    break;
  default:
    break;
  }
}

static uint16_t alu_add(struct Machine *machine, uint16_t a, uint16_t b)
{
  uint32_t sum = (uint32_t)a + b;
  uint16_t result = (uint16_t)sum;

  machine->flags = (enum Flag)(machine->flags & ~(FLAG_C | FLAG_V));
  if (sum > 0xFFFF)
    machine->flags = (enum Flag)(machine->flags | FLAG_C);
  if ((uint16_t)(~(a ^ b) & (a ^ result)) & 0x8000)
    machine->flags = (enum Flag)(machine->flags | FLAG_V);
  return result;
}

static uint16_t alu_sub(struct Machine *machine, uint16_t a, uint16_t b)
{
  uint32_t difference = (uint32_t)a - b;
  uint16_t result = (uint16_t)difference;

  machine->flags = (enum Flag)(machine->flags & ~(FLAG_C | FLAG_V));
  if (a < b)
    machine->flags = (enum Flag)(machine->flags | FLAG_C);
  if ((uint16_t)((a ^ b) & (a ^ result)) & 0x8000)
    machine->flags = (enum Flag)(machine->flags | FLAG_V);
  return result;
}

static uint16_t alu_logic(struct Machine *machine, uint16_t result)
{
  machine->flags = (enum Flag)(machine->flags & ~(FLAG_C | FLAG_V));
  return result;
}

static void update_zn_flags(struct Machine *machine, uint16_t result)
{
  machine->flags = (enum Flag)(machine->flags & ~(FLAG_Z | FLAG_N));
  if (result == 0)
    machine->flags = (enum Flag)(machine->flags | FLAG_Z);
  if (result & 0x8000)
    machine->flags = (enum Flag)(machine->flags | FLAG_N);
}

static void execute_alu(struct Machine *machine, const struct Instruction *instruction)
{
  uint16_t ord0 = read_register(machine, ORD0);
  uint16_t ord1 = read_register(machine, ORD1);
  uint16_t result;

  switch (instruction->opcode)
  {
  case ADD:
    result = alu_add(machine, ord0, ord1);
    break;
  case SUB:
  case CMP:
    result = alu_sub(machine, ord0, ord1);
    break;
  case DIV:
    if (ord1 == 0)
    {
      machine->fault = FAULT_DIV_ZERO;
      return;
    }
    result = alu_logic(machine, (uint16_t)(ord0 / ord1));
    break;
  case INC:
    result = alu_add(machine, ord0, 1);
    break;
  case DEC:
    result = alu_sub(machine, ord0, 1);
    break;
  case AND:
    result = alu_logic(machine, ord0 & ord1);
    break;
  case OR:
    result = alu_logic(machine, ord0 | ord1);
    break;
  case XOR:
    result = alu_logic(machine, ord0 ^ ord1);
    break;
  case NOT:
    result = alu_logic(machine, (uint16_t)~ord0);
    break;
  case SHL:
    result = alu_logic(machine, ord1 < 16 ? (uint16_t)(ord0 << ord1) : 0);
    break;
  case SHR:
    result = alu_logic(machine, ord1 < 16 ? (uint16_t)(ord0 >> ord1) : 0);
    break;
  default:
    return;
  }

  if (instruction->opcode != CMP)
    write_register(machine, RS, result);

  update_zn_flags(machine, result);
}

static bool trace_enabled = true;

void enable_instruction_trace(bool enabled)
{
  trace_enabled = enabled;
}

static void trace_instruction(const struct Machine *machine, uint16_t pc, const struct Instruction *instruction)
{
  if (!trace_enabled)
    return;

  char text[32];
  format_instruction(instruction, text, sizeof(text));
  MACHINE_LOG("%04X: %04X  %s", pc, machine->reg[IR], text);
}

bool step_machine(struct Machine *machine)
{
  struct Instruction instruction;

  if (machine->halted || machine->fault != FAULT_NONE)
    return false;

  uint16_t pc = read_register(machine, PC);
  size_t size = decode_instruction(machine->memory, pc, &instruction);
  machine->reg[IR] = fetch(machine->memory, pc);
  write_register(machine, PC, (uint16_t)(pc + size));

  trace_instruction(machine, pc, &instruction);
  execute(machine, &instruction);

  if (trace_enabled && machine->fault != FAULT_NONE)
    MACHINE_LOG("FAULT %s at %04X", machine->fault == FAULT_DIV_ZERO ? "DIV_ZERO" : "UNKNOWN", pc);
  else if (trace_enabled && instruction.opcode == HLT)
    MACHINE_LOG("HALT at %04X", read_register(machine, PC));

  return true;
}

void run_machine(struct Machine *machine)
{
  uint32_t steps = 0;
  while (step_machine(machine))
  {
    if (++steps >= MACHINE_RUN_STEP_LIMIT)
    {
      MACHINE_LOG("RUN aborted: step limit %lu reached", (unsigned long)steps);
      break;
    }
  }
}

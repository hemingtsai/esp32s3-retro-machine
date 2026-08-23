#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define MEMORY_WORD_COUNT 0x10000

enum Register
{
  ORD0 = 0b0000,
  ORD1 = 0b0001,
  PC = 0b0010,
  IR = 0b0011,
  MAR = 0b0100,
  SP = 0b0101,
  RS = 0b0110,
  DISPLAY = 0b0111,
  R0 = 0b1000,
  R1 = 0b1001,
  R2 = 0b1010,
  R3 = 0b1011,
  R4 = 0b1100,
  R5 = 0b1101,
  R6 = 0b1110,
  R7 = 0b1111
};

struct Instruction
{
  enum InstructionType
  {
    R,
    U,
    J,
    I,
    N
  } type;
  enum Opcode
  {
    MOV = 0b00000,
    LDI = 0b00001,
    RED = 0b00010,
    WRT = 0b00011,
    PUSH = 0b00100,
    POP = 0b00101,
    MNXT = 0b00110,
    MPRV = 0b00111,
    ADD = 0b01000,
    SUB = 0b01001,
    DIV = 0b01010,
    INC = 0b01011,
    DEC = 0b01100,
    AND = 0b01101,
    OR = 0b01110,
    XOR = 0b01111,
    NOT = 0b10000,
    SHL = 0b10001,
    SHR = 0b10010,
    CMP = 0b10011,
    JMP = 0b10100,
    JZ = 0b10101,
    JNZ = 0b10110,
    JN = 0b10111,
    JP = 0b11000,
    JC = 0b11001,
    JNC = 0b11010,
    JV = 0b11011,
    CALL = 0b11100,
    RET = 0b11101,
    NOP = 0b11110,
    HLT = 0b11111
  } opcode;
  union
  {
    struct RTypeArguments
    {
      enum Register source;
      enum Register destination;
    } r;
    struct UTypeArguments
    {
      enum Register reg;
    } u;
    struct JTypeArguments
    {
      enum Register target;
    } j;
    struct ITypeArguments
    {
      enum Register destination;
      uint16_t immediate;
    } i;
  };
};

static uint16_t fetch(const uint16_t memory[], uint16_t pc)
{
  return memory[pc];
}

enum Fault
{
  FAULT_NONE,
  FAULT_DIV_ZERO
};

struct Machine
{
  uint16_t memory[MEMORY_WORD_COUNT];
  uint16_t reg[16];
  enum Flag
  {
    FLAG_Z = 1 << 0,
    FLAG_N = 1 << 1,
    FLAG_C = 1 << 2,
    FLAG_V = 1 << 3
  } flags;
  bool halted;
  enum Fault fault;
};

void reset_machine(struct Machine *machine)
{
  memset(machine->memory, 0, sizeof(machine->memory));
  memset(machine->reg, 0, sizeof(machine->reg));
  machine->flags = (enum Flag)0;
  machine->halted = false;
  machine->fault = FAULT_NONE;
}

uint16_t read_register(const struct Machine *machine, enum Register reg)
{
  return machine->reg[reg];
}

void write_register(struct Machine *machine, enum Register reg, uint16_t value)
{
  machine->reg[reg] = value;
}

enum PanelDisplay
{
  PANEL_PC,
  PANEL_MAR,
  PANEL_IR,
  PANEL_R0,
  PANEL_R1,
  PANEL_R2,
  PANEL_R3,
  PANEL_RS,
  PANEL_MEMORY
};

enum PanelInputMode
{
  PANEL_LOW_BYTE,
  PANEL_HIGH_BYTE
};

struct ControlPanel
{
  enum PanelDisplay display;
  enum PanelInputMode input_mode;
  uint8_t switches;
  uint16_t input_latch;
};

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

static void execute_data(struct Machine *machine, const struct Instruction *instruction);
static void execute_alu(struct Machine *machine, const struct Instruction *instruction);

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

void run_machine(struct Machine *machine)
{
  struct Instruction instruction;

  while (!machine->halted && machine->fault == FAULT_NONE)
  {
    uint16_t pc = read_register(machine, PC);
    size_t size = decode_instruction(machine->memory, pc, &instruction);
    machine->reg[IR] = fetch(machine->memory, pc);
    write_register(machine, PC, (uint16_t)(pc + size));
    execute(machine, &instruction);
  }
}

void app_main(void)
{
  static struct Machine machine;
  reset_machine(&machine);

  static const uint16_t program[] = {
      0x0C00, 0x0005,
      0x0C80, 0x0007,
      0x0400,
      0x0488,
      0x4000,
      0x0350,
      0x0D80, 0x2000,
      0x05A0,
      0x1D00,
      0xF800,
  };
  memcpy(machine.memory, program, sizeof(program));

  machine.reg[SP] = 0x8000;
  run_machine(&machine);

  printf("halted=%d fault=%d\n", machine.halted, machine.fault);
  printf("R2=%04Xh memory[2000h]=%04Xh\n", read_register(&machine, R2), machine.memory[0x2000]);
}

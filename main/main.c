#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

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

void app_main(void) {}

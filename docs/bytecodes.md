# 字节码

## 基本定义

- 数据宽度：16 bit
- 地址宽度：16 bit
- 指令长度：16 bit
- Opcode：5 bit
- Opcode 数量：32
- 字节序：Little Endian
- 程序计数器 `PC` 以 16 bit instruction word 为单位递增

每条指令默认：

```text
IR ← Memory[PC]
PC ← PC + 1
````

如果指令使用扩展字，则继续读取下一条 16 bit word。

# 寄存器编码

所有可访问寄存器使用 4 bit 编码。

```text
0000  ORD0
0001  ORD1
0010  PC
0011  IR
0100  MAR
0101  SP
0110  RS
0111  DISPLAY

1000  R0
1001  R1
1010  R2
1011  R3
1100  R4
1101  R5
1110  R6
1111  R7
```

# 指令格式

## R-Type

用于两个寄存器操作数。

```text
15       11 10        7 6        3 2       0
┌─────────┬────────────┬───────────┬─────────┐
│  OPCODE │   SOURCE   │    DEST   │   RSV   │
│  5 bit  │   4 bit    │   4 bit   │  3 bit  │
└─────────┴────────────┴───────────┴─────────┘
```

## U-Type

用于一个寄存器操作数。

```text
15       11 10        7 6                   0
┌─────────┬────────────┬─────────────────────┐
│  OPCODE │  REGISTER  │         RSV         │
│  5 bit  │   4 bit    │        7 bit        │
└─────────┴────────────┴─────────────────────┘
```

## J-Type

用于控制流指令。

```text
15       11 10        7 6                   0
┌─────────┬────────────┬─────────────────────┐
│  OPCODE │   TARGET   │         RSV         │
│  5 bit  │   4 bit    │        7 bit        │
└─────────┴────────────┴─────────────────────┘
```

`TARGET` 指定保存目标地址的寄存器。

## I-Type

用于 16 位立即数。

第一字：

```text
15       11 10        7 6                   0
┌─────────┬────────────┬─────────────────────┐
│  OPCODE │    DEST    │         RSV         │
│  5 bit  │   4 bit    │        7 bit        │
└─────────┴────────────┴─────────────────────┘
```

第二字：

```text
15                                        0
┌──────────────────────────────────────────┐
│                IMMEDIATE                 │
│                  16 bit                  │
└──────────────────────────────────────────┘
```

# Opcode

```text
00000  MOV
00001  LDI
00010  RED
00011  WRT
00100  PUSH
00101  POP

00110  MNXT
00111  MPRV

01000  ADD
01001  SUB
01010  DIV
01011  INC
01100  DEC

01101  AND
01110  OR
01111  XOR
10000  NOT

10001  SHL
10010  SHR

10011  CMP

10100  JMP
10101  JZ
10110  JNZ
10111  JN
11000  JP
11001  JC
11010  JNC
11011  JV

11100  CALL
11101  RET

11110  NOP
11111  HLT
```

共 32 条。

# 数据传输

## MOV

```text
MOV source, destination
```

使用 R-Type。

将 `source` 的值复制到 `destination`。

例如：

```text
MOV R0, R1
```

执行：

```text
R1 ← R0
```

`source` 和 `destination` 可以为可访问的 16 位寄存器。

`IR` 和 `FLAGS` 不允许作为 `destination`。

## LDI

```text
LDI destination, immediate
```

使用 I-Type。

将 16 位立即数写入 `destination`。

例如：

```text
LDI R0, 1234h
```

编码为两个 16 bit words：

```text
第一字：
LDI R0

第二字：
1234h
```

执行：

```text
R0 ← 1234h
PC ← PC + 2
```

`destination` 仅允许为通用寄存器 `R0～R7`。

## RED

```text
RED destination
```

使用 U-Type。

读取 `MAR` 指向的内存，并将数据写入 `destination`。

```text
destination ← Memory[MAR]
```

## WRT

```text
WRT source
```

使用 U-Type。

将 `source` 的值写入 `MAR` 指向的内存。

```text
Memory[MAR] ← source
```

## PUSH

```text
PUSH source
```

将 `source` 的值压入栈中。

```text
SP ← SP - 1
Memory[SP] ← source
```

## POP

```text
POP destination
```

弹出栈顶数据并写入 `destination`。

```text
destination ← Memory[SP]
SP ← SP + 1
```

# 内存操作

## MNXT

```text
MNXT
```

将 `MAR` 加一。

```text
MAR ← MAR + 1
```

## MPRV

```text
MPRV
```

将 `MAR` 减一。

```text
MAR ← MAR - 1
```

# 算术运算

算术运算使用 `ORD0` 和 `ORD1` 作为操作数，结果写入 `RS`。

所有算术运算都会更新 `FLAGS`。

## ADD

```text
ADD
```

```text
RS ← ORD0 + ORD1
```

## SUB

```text
SUB
```

```text
RS ← ORD0 - ORD1
```

## DIV

```text
DIV
```

```text
RS ← ORD0 ÷ ORD1
```

除数为 0 时产生除零异常。

## INC

```text
INC
```

```text
RS ← ORD0 + 1
```

## DEC

```text
DEC
```

```text
RS ← ORD0 - 1
```

# 逻辑运算

逻辑运算使用 `ORD0` 和 `ORD1` 作为操作数，结果写入 `RS`。

所有逻辑运算都会更新 `FLAGS`。

## AND

```text
AND
```

```text
RS ← ORD0 AND ORD1
```

## OR

```text
OR
```

```text
RS ← ORD0 OR ORD1
```

## XOR

```text
XOR
```

```text
RS ← ORD0 XOR ORD1
```

## NOT

```text
NOT
```

```text
RS ← NOT ORD0
```

# 移位

## SHL

```text
SHL
```

将 `ORD0` 左移 `ORD1` 位。

```text
RS ← ORD0 << ORD1
```

更新 `FLAGS`。

## SHR

```text
SHR
```

将 `ORD0` 逻辑右移 `ORD1` 位。

```text
RS ← ORD0 >> ORD1
```

更新 `FLAGS`。

# 比较

## CMP

```text
CMP
```

计算：

```text
ORD0 - ORD1
```

仅更新 `FLAGS`，不修改 `RS`。

# 控制流

所有控制流指令使用 J-Type。

目标地址由 `TARGET` 寄存器提供。

因此 CPU 可以访问完整的 16 位地址空间。

## JMP

```text
JMP target
```

无条件跳转。

```text
PC ← target
```

例如：

```text
JMP R0
```

表示：

```text
PC ← R0
```

## JZ

```text
JZ target
```

当 `FLAGS.Z = 1` 时：

```text
PC ← target
```

## JNZ

```text
JNZ target
```

当 `FLAGS.Z = 0` 时：

```text
PC ← target
```

## JN

```text
JN target
```

当 `FLAGS.N = 1` 时：

```text
PC ← target
```

## JP

```text
JP target
```

当 `FLAGS.N = 0` 时：

```text
PC ← target
```

## JC

```text
JC target
```

当 `FLAGS.C = 1` 时：

```text
PC ← target
```

## JNC

```text
JNC target
```

当 `FLAGS.C = 0` 时：

```text
PC ← target
```

## JV

```text
JV target
```

当 `FLAGS.V = 1` 时：

```text
PC ← target
```

## CALL

```text
CALL target
```

将下一条指令的地址压入栈，并跳转到 `target`。

```text
SP ← SP - 1
Memory[SP] ← PC
PC ← target
```

## RET

```text
RET
```

从栈中恢复返回地址。

```text
PC ← Memory[SP]
SP ← SP + 1
```

# 其他

## NOP

```text
NOP
```

不执行任何操作。

## HLT

```text
HLT
```

停止 CPU 执行。

CPU 停止后，只有 `RESET` 或外部控制操作可以使 CPU 恢复执行。

# FLAGS

`FLAGS` 为 4 bit。

```text
┌───┬───┬───┬───┐
│ V │ C │ N │ Z │
└───┴───┴───┴───┘
```

## Z

结果为 0 时置 1。

## N

结果最高位为 1 时置 1。

## C

无符号运算产生进位或借位时置 1。

## V

有符号运算发生溢出时置 1。

# 指令执行

CPU 的基本执行周期：

```text
FETCH
  │
  ▼
IR ← Memory[PC]
  │
  ▼
PC ← PC + 1
  │
  ▼
DECODE
  │
  ▼
EXECUTE
```

对于 `LDI`：

```text
FETCH
  │
  ▼
IR ← Memory[PC]
PC ← PC + 1
  │
  ▼
DECODE
  │
  ▼
IMMEDIATE FETCH
  │
  ▼
立即数 ← Memory[PC]
PC ← PC + 1
  │
  ▼
EXECUTE
```

因此：

```text
普通指令：1 word
LDI：      2 words
```

所有存储单元均为 16 bit word。
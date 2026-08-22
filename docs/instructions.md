# 指令

## 数据传输

### MOV

```text
MOV source, destination
````

将 `source` 的值复制到 `destination`，其中，`source`为任意寄存器或者任意16位常数， `destination` 为任意寄存器。

### RED

```text
RED destination
```

读取 `MAR` 指向的内存，并将数据写入 `destination`。

### WRT

```text
WRT source
```

将 `source` 的值写入 `MAR` 指向的内存。

### PUSH

```text
PUSH source
```

将 `source` 的值压入栈中。

### POP

```text
POP destination
```

将栈顶的数据弹出，并写入 `destination`。

## 内存操作

### MNXT

```text
MNXT
```

将 `MAR` 加一。

### MPRV

```text
MPRV
```

将 `MAR` 减一。

## 算术运算

### ADD

```text
ADD
```

计算 `ORD0 + ORD1`，结果写入 `RS`，并更新 `FLAGS`。

### SUB

```text
SUB
```

计算 `ORD0 - ORD1`，结果写入 `RS`，并更新 `FLAGS`。


### DIV

```text
DIV
```

计算 `ORD0 ÷ ORD1`，结果写入 `RS`，并更新 `FLAGS`。

### INC

```text
INC
```

计算 `ORD0 + 1`，结果写入 `RS`，并更新 `FLAGS`。

### DEC

```text
DEC
```

计算 `ORD0 - 1`，结果写入 `RS`，并更新 `FLAGS`。

## 逻辑运算

### AND

```text
AND
```

计算 `ORD0 AND ORD1`，结果写入 `RS`，并更新 `FLAGS`。

### OR

```text
OR
```

计算 `ORD0 OR ORD1`，结果写入 `RS`，并更新 `FLAGS`。

### XOR

```text
XOR
```

计算 `ORD0 XOR ORD1`，结果写入 `RS`，并更新 `FLAGS`。

### NOT

```text
NOT
```

计算 `NOT ORD0`，结果写入 `RS`，并更新 `FLAGS`。

## 移位

### SHL

```text
SHL
```

将 `ORD0` 左移 `ORD1` 位，结果写入 `RS`，并更新 `FLAGS`。

### SHR

```text
SHR
```

将 `ORD0` 右移 `ORD1` 位，结果写入 `RS`，并更新 `FLAGS`。

## 比较

### CMP

```text
CMP
```

计算 `ORD0 - ORD1`，更新 `FLAGS`，不修改 `RS` 和通用寄存器。

## 控制流

### JMP

```text
JMP address
```

将 `PC` 设置为 `address`，`address`可以为通用寄存器。

### JZ

```text
JZ address
```

当 `FLAGS.Z` 为 `1` 时，将 `PC` 设置为 `address`。

### JNZ

```text
JNZ address
```

当 `FLAGS.Z` 为 `0` 时，将 `PC` 设置为 `address`。

### JN

```text
JN address
```

当 `FLAGS.N` 为 `1` 时，将 `PC` 设置为 `address`。

### JP

```text
JP address
```

当 `FLAGS.N` 为 `0` 时，将 `PC` 设置为 `address`。

### JC

```text
JC address
```

当 `FLAGS.C` 为 `1` 时，将 `PC` 设置为 `address`。

### JNC

```text
JNC address
```

当 `FLAGS.C` 为 `0` 时，将 `PC` 设置为 `address`。

### JV

```text
JV address
```

当 `FLAGS.V` 为 `1` 时，将 `PC` 设置为 `address`。

### CALL

```text
CALL address
```

将下一条指令的地址压入栈中，并将 `PC` 设置为 `address`。

### RET

```text
RET
```

从栈中取出返回地址，并将其写入 `PC`。

## 其他

### NOP

```text
NOP
```

不执行任何操作。

### HLT

```text
HLT
```

停止 CPU 执行。
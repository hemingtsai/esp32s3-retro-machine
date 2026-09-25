.ENTRY start
.text ignored
start: first: second:
.word 10, 0x2a, 0b1010, 0o17, -0, 0FFh
WORD label, 65535
.data ignored
LDI -0, R0
LDi 0x1234, r1
lDi label, R2
MOV R0, ORD0
mov IR, PC
RED display
WRT SP
PUSH R3
POP R4
MNXT
MPRV
ADD
SUB
DIV
INC
DEC
AND
OR
XOR
NOT
SHL
SHR
CMP
JMP R5
JZ label
JNZ first
JN second
JP label
JC R6
JNC R7
JV R0
CALL label
RET
NOP
HLT
label:

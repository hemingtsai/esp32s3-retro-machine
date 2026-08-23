; Count down from 3 to 1 using DEC, then halt with Z set.
  LDI 3, R0
loop:
  MOV R0, ORD0
  LDI 0, R1
  MOV R1, ORD1
  DEC
  JZ done
  MOV RS, R0
  JMP loop
done:
  HLT

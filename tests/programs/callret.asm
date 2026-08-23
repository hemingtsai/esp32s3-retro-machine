; CALL a subroutine by label (pseudo-instruction expansion),
; compute 20 + 22 in the subroutine, return and store the
; result into a data word, then halt.
  LDI sub, R7
  CALL R7
after:
  MOV RS, R2
  LDI result, R3
  MOV R3, MAR
  WRT R2
  HLT
sub:
  LDI 20, R0
  LDI 22, R1
  MOV R0, ORD0
  MOV R1, ORD1
  ADD
  RET

result:
  .word 0

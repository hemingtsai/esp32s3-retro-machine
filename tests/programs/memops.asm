; Memory round trip: write a pattern with WRT/MNXT, read it back
; with RED/MPRV into R4/R5 and compute a checksum into RS.
data:
  .word 0
  .word 0

start:
  LDI data, R1
  MOV R1, MAR
  LDI A5h, R2
  WRT R2
  MNXT
  LDI 5Ah, R3
  WRT R3
  MPRV
  RED R4          ; expect A5h
  MNXT
  RED R5          ; expect 5Ah
  MOV R4, ORD0
  MOV R5, ORD1
  ADD             ; A5h + 5Ah = FFh, N=0 C=0 V=0 Z=0
  HLT

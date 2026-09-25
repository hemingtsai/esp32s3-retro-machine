#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../main/machine.c"

#define RUNNER_STEP_LIMIT 1000000UL

int main(int argc, char **argv)
{
    struct Machine machine;
    FILE *file;
    unsigned char buffer[4096];
    size_t count;
    size_t total = 0u;
    unsigned long steps = 0u;
    bool read_error;
    bool close_error;

    if (argc != 2) {
        fprintf(stderr, "usage: compiler_runner PROGRAM.bin\n");
        return 2;
    }
    file = fopen(argv[1], "rb");
    if (file == NULL) {
        perror(argv[1]);
        return 1;
    }
    reset_machine(&machine);
    enable_instruction_trace(false);
    while ((count = fread(buffer, 1u, sizeof(buffer), file)) != 0u) {
        size_t index;
        if (total + count > sizeof(machine.memory)) {
            fprintf(stderr, "program exceeds machine memory\n");
            fclose(file);
            return 1;
        }
        for (index = 0u; index < count; index += 2u) {
            if (index + 1u >= count) {
                fprintf(stderr, "program has odd byte length\n");
                fclose(file);
                return 1;
            }
            machine.memory[(total + index) / 2u] =
                (uint16_t)buffer[index] | ((uint16_t)buffer[index + 1u] << 8);
        }
        total += count;
    }
    read_error = ferror(file) != 0;
    close_error = fclose(file) != 0;
    if (read_error || close_error) {
        fprintf(stderr, "unable to read program\n");
        return 1;
    }
    machine.reg[PC] = 0u;
    machine.halted = false;
    machine.fault = FAULT_NONE;
    while (!machine.halted && machine.fault == FAULT_NONE && steps < RUNNER_STEP_LIMIT) {
        if (!step_machine(&machine)) {
            break;
        }
        ++steps;
    }
    printf("RS=%04X R0=%04X R1=%04X R2=%04X R3=%04X R4=%04X R5=%04X "
           "R6=%04X R7=%04X HALTED=%d FAULT=%d STEPS=%lu\n",
           machine.reg[RS], machine.reg[R0], machine.reg[R1], machine.reg[R2],
           machine.reg[R3], machine.reg[R4], machine.reg[R5], machine.reg[R6],
           machine.reg[R7], machine.halted ? 1 : 0, machine.fault, steps);
    return machine.halted && machine.fault == FAULT_NONE ? 0 : 1;
}

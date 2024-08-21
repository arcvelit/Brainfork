#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "Instructions.h"

#define MEMORY_BUFFER_CAP 1024

typedef char byte;

struct { uint64_t no_instructions;} stats = {0};


void print_buffer(byte* buff, const size_t len)
{
    const byte* end = buff + len;
    while (buff < end)
        putchar(*buff++);
}

void print_loop_stack(Instruction** loop_stack, uint64_t loop_stack_pointer)
{
    for (int i = 0; i < loop_stack_pointer; i++)
    {
        printf("%c", loop_stack[i]->_char);
    }
    printf("\n");
}

void push_loop_stack(Instruction** loop_stack, Instruction* instruction, uint64_t* loop_stack_pointer)
{
    loop_stack[(*loop_stack_pointer)++] = instruction;
}

Instruction* pop_loop_stack(Instruction** loop_stack, uint64_t* loop_stack_pointer)
{
    return loop_stack[--(*loop_stack_pointer)];
}

FILE* get_file_handle(int argc, char* argv[])
{
    if (argc < 2)
    {
        printf("error: must provide a source file");
        exit(EXIT_FAILURE);
    }
    else if (argc > 2)
    {
        printf("warning: unnecessary argument '%s'\n", argv[2]);
    }

    FILE *file = fopen(argv[1], "r");

    if (file == NULL) 
    {
        printf("error: could not open %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    return file;
}

void parse_to_instructions(Instruction* instruction_buff, FILE* file, const size_t file_length)
{
    char c; 
    uint64_t loop_stack_pointer = 0;
    Instruction** loop_stack = malloc(sizeof(Instruction) * file_length);

    while ((c = fgetc(file)) != EOF)
    {   
        Instruction* instruction = malloc(sizeof(Instruction)); 
        instruction->_char = c;

        switch (instruction->_char)
        {
            case '+':
                instruction->_op = OP_INCREMENT;
                break;
            case '-':
                instruction->_op = OP_DECREMENT;
                break;
            case '[':
                instruction->_op = OP_BRACKET_OPEN;
                instruction->_position = stats.no_instructions;
                push_loop_stack(loop_stack, instruction, &loop_stack_pointer);
                break;
            case ']':
                instruction->_op = OP_BRACKET_CLOSED;
                if (loop_stack_pointer == 0)
                {
                    printf("error: misaligned loop brackets");
                    exit(EXIT_FAILURE);
                }
                Instruction* i = pop_loop_stack(loop_stack, &loop_stack_pointer);
                instruction->_point_to = i->_position;
                break;
            case '.':
                instruction->_op = OP_PRINT;
                break;
            case '>':
                instruction->_op = OP_MOVE_RIGHT;
                break;
            case '<':
                instruction->_op = OP_MOVE_LEFT;
                break;
            case '\n': 
            case '\r':
            case '\t':
            case  ' ':
                continue; 
            default:
                printf("error: unsupported instruction '%s'", instruction->_char);
                exit(EXIT_FAILURE);
        }

        *instruction_buff++ = *instruction;
        stats.no_instructions++;
    }

    free(loop_stack);
}


int main(int argc, char *argv[])
{
    // Setup instructions
    FILE* file = get_file_handle(argc, argv);

    fseek (file, 0, SEEK_END);
    const uint32_t file_length = ftell(file);
    fseek (file, 0, SEEK_SET);

    Instruction* instruction_buffer = malloc(sizeof(Instruction) * file_length);

    parse_to_instructions(instruction_buffer, file, file_length);
    fclose(file);


    // Start running the program
    byte memory_buffer[MEMORY_BUFFER_CAP];
    memset(&memory_buffer, 0, MEMORY_BUFFER_CAP);

    uint64_t program_counter = 0;
    uint64_t stack_pointer = 0;

    while(program_counter < stats.no_instructions)
    {
        Instruction instruction = instruction_buffer[program_counter];

        switch (instruction._op)
        {
            case OP_INCREMENT:
                memory_buffer[stack_pointer]++;
                break;
            case OP_DECREMENT:
                memory_buffer[stack_pointer]--;
                break;
            case OP_BRACKET_OPEN:
                break;
            case OP_BRACKET_CLOSED:
                if (memory_buffer[stack_pointer] != 0)
                {
                    program_counter = instruction._point_to;
                    continue;
                }
                break;
            case OP_PRINT:
                printf("%c", memory_buffer[stack_pointer]);
                break;
            case OP_MOVE_RIGHT:
                stack_pointer = (stack_pointer + 1) % MEMORY_BUFFER_CAP;
                break;
            case OP_MOVE_LEFT:
                stack_pointer = (stack_pointer + MEMORY_BUFFER_CAP - 1) % MEMORY_BUFFER_CAP;
                break;
        }

        program_counter++;

    }

    free(instruction_buffer);
}
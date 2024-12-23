#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>


#define USAGE "USAGE: brainfork <option: -c -i -t> <filename>"

#define BF_MEMORY_STRIP_SIZE        1024
#define BF_INTERACTIVE_INPUT_SIZE    256
#define BF_BUFFER_INITIAL_CAP         64

#define BF_UNUSED(VAR) (void)(VAR)

#define BF_IS_WHITESPACE(c)  ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r' || (c) == '\f' || (c) == '\v')
#define BF_SEEK_COMMENT(ptr) (*(ptr) == '/' && *((ptr)+1) == '/')
#define BF_IS_END_COMMENT(c)  ((c) == '\n' || (c) == '\0')

#define BF_CC_COMPILE_FMT(f) "cc %s.c -o %s", f, f
#define BF_GCC_COMPILE_FMT(f) "gcc %s.c -o %s", f, f
#define BF_CLANG_COMPILE_FMT(f) "clang %s.c -o %s", f, f
#define BF_MSVC_COMPILE_FMT(f) "cl %s.c /Fe%s.exe", f, f

#if defined(_WIN32) || defined(_WIN64)
    #define FIND_COMMAND(cmd) "where " cmd
#else
    #define FIND_COMMAND(cmd) "command -v " cmd
#endif

#define BF_WRITE_COMMAND_BUF(BUF, FMT) snprintf(BUF, sizeof(BUF), FMT)

#define BF_MEM_ASSERT(PTR)                           \
    do {                                             \
        if (!(PTR)) {                                \
            fprintf(stderr, "FATAL: Out of memory"); \
            exit(EXIT_FAILURE);                      \
        }                                            \
    } while (0)


typedef char byte;

typedef enum
{
    OPTION_COMPILE,
    OPTION_INTERPRET,
    OPTION_TRANSPILE
} Option;

typedef enum 
{
    OP_WRITE,
    OP_BRACKET_OPEN,
    OP_BRACKET_CLOSED,
    OP_PRINT,
    OP_SHIFT,
} Operation;

typedef struct 
{
    Operation _op;
    union 
    {
        size_t _point_to;
        size_t _position;
        long mov;
        long inc;
    };
} Instruction;

typedef struct 
{
    size_t pointer;
    Instruction* stack;
} LoopStack;


// Takes a `file.abc` and returns new string `file.[ext]`
char* strdup_dot_x(const char* _cstr, char ext) 
{
    if (!_cstr) return NULL; 

    char* last_dot = strrchr(_cstr, '.');
    if (last_dot++ && *last_dot != '\0')
    {
        int offset = last_dot - _cstr;
        char* new_str = strdup(_cstr);
        BF_MEM_ASSERT(new_str);

        new_str[offset++] = ext;
        new_str[offset]   = '\0';
        return new_str;
    }
    
    return NULL;
}

// Takes a `file.abc` and returns new string `file`
char* strdup_dot_x_strip(const char* _cstr) {
    if (!_cstr) return NULL;

    char* last_dot = strrchr(_cstr, '.');
    if (last_dot) 
    {
        char* new_str = strdup(_cstr);
        BF_MEM_ASSERT(new_str);

        int offset = last_dot - _cstr;
        new_str[offset] = '\0';
        return new_str;
    }

    return NULL;
}

void push_loop_stack(LoopStack* loop_stack, Instruction instruction)
{
    loop_stack->stack[loop_stack->pointer++] = instruction;
}

Instruction pop_loop_stack(LoopStack* loop_stack)
{
    return loop_stack->stack[--loop_stack->pointer];
}

void allocate_loop_stack(LoopStack* loop_stack, size_t program_size)
{
    loop_stack->stack = malloc(sizeof(Instruction) * program_size / 2 + 1);
    loop_stack->pointer = 0;
}

void free_loop_stack(LoopStack* loop_stack)
{
    free(loop_stack->stack);
}

void get_program_info(int argc, char* argv[], Option* option, const char** file_name)
{
    if (argc < 2)
    {
        printf(USAGE);
        exit(EXIT_FAILURE);
    }
    else if (argc > 3)
    {
        fprintf(stderr, "WARNING: Unnecessary argument '%s'\n", argv[3]);
    }

    // Get option
    if (strcmp(argv[1], "-c") == 0)
    {
        *option = OPTION_COMPILE;
    }
    else if (strcmp(argv[1], "-i") == 0)
    {
        *option = OPTION_INTERPRET;
    }
    else if (strcmp(argv[1], "-t") == 0)
    {
        *option = OPTION_TRANSPILE;
    }
    else 
    {
        printf(USAGE);
        exit(EXIT_FAILURE);
    }

    *file_name = argv[2];
}

Instruction* parse_instructions(char* program_content, size_t* len)
{
    // Loop stack holds opening brackets
    LoopStack loop_stack;
    allocate_loop_stack(&loop_stack, strlen(program_content));

    // Instructions holds ... instructions
    size_t instructions_size = 0;
    size_t instructions_cap = BF_BUFFER_INITIAL_CAP;
    Instruction* instructions = malloc(sizeof(Instruction) * instructions_cap);
    BF_MEM_ASSERT(instructions);

    // Optimize increments and shifts
    long mov_accumulator = 0;
    long inc_accumulator = 0;
    
    while (*program_content)
    {   

        Instruction instruction = {0};

        // Consume whitespace and comments

        if (BF_IS_WHITESPACE(*program_content))
        {
            do program_content++; while (BF_IS_WHITESPACE(*program_content));
        }
        if (BF_SEEK_COMMENT(program_content))
        {
            program_content++;
            do program_content++; while(!BF_IS_END_COMMENT(*program_content));
            program_content += *program_content == '\n';
        }

        // Minimize increment instructions

        if (inc_accumulator != 0 && *program_content != '+' && *program_content != '-')
        {
            instruction._op = OP_WRITE;
            instruction.inc = inc_accumulator;
            inc_accumulator = 0;
            program_content--;
            goto PUSH_TOKEN;
        }

        // Minimize increment instructions
        if (mov_accumulator != 0 && *program_content != '<' && *program_content != '>')
        {
            instruction._op = OP_SHIFT;
            instruction.mov = mov_accumulator;
            mov_accumulator = 0;
            program_content--;
            goto PUSH_TOKEN;
        }

        if (!*program_content) goto DEFER_END;

        switch (*program_content)
        {
            case '+':
                inc_accumulator++;
                goto DEFER_SKIP;
            case '-':
                inc_accumulator--;
                goto DEFER_SKIP;
            case '[':
                instruction._op = OP_BRACKET_OPEN;
                instruction._position = instructions_size;
                push_loop_stack(&loop_stack, instruction);
                break;
            case ']':
                instruction._op = OP_BRACKET_CLOSED;
                if (loop_stack.pointer == 0)
                {
                    fprintf(stderr, "ERROR: Misaligned loop brackets\n");
                    free_loop_stack(&loop_stack);
                    free(instructions);
                    return NULL;
                }
                Instruction i = pop_loop_stack(&loop_stack);
                instruction._point_to = i._position;
                break;
            case '.':
                instruction._op = OP_PRINT;
                break;
            case '>':
                mov_accumulator++;
                goto DEFER_SKIP;
            case '<':
                mov_accumulator--;
                goto DEFER_SKIP;
            default:
                fprintf(stderr, "ERROR: Unknown token `%c`\n", *program_content);
                free_loop_stack(&loop_stack);
                free(instructions);
                return NULL;
        }

        // Push into instructions
        PUSH_TOKEN:
        if (instructions_size == instructions_cap) 
        {
            size_t new_capacity = instructions_cap * 2;
            Instruction* new_instructions = realloc(instructions, sizeof(Instruction) * new_capacity );
            BF_MEM_ASSERT(new_instructions);

            instructions_cap = new_capacity;
            instructions = new_instructions;
        }
        
        instructions[instructions_size++] = instruction;

        DEFER_SKIP:
        program_content++;
    }

    DEFER_END:
    if (loop_stack.pointer != 0)
    {
        fprintf(stderr, "ERROR: Misaligned loop brackets\n");
        free_loop_stack(&loop_stack);
        free(instructions);
        return NULL;
    }

    *len = instructions_size;
    return instructions;
}

void interpret_bf(Instruction* instruction_buffer, size_t no_instructions, byte* memory_buffer, size_t* cursor, size_t buffer_size)
{
    /* Instruction */ size_t program_counter = 0;
    /* Memory      */ size_t stack_pointer   = *cursor;

    while(program_counter < no_instructions)
    {
        Instruction instruction = instruction_buffer[program_counter];

        switch (instruction._op)
        {
            case OP_WRITE:
                memory_buffer[stack_pointer] += instruction.inc;
                break;
            case OP_BRACKET_OPEN:
                break;
            case OP_BRACKET_CLOSED:
                if (memory_buffer[stack_pointer] == 0) break;
                program_counter = instruction._point_to;
                continue;
            case OP_PRINT:
                putchar(memory_buffer[stack_pointer]);
                break;
            case OP_SHIFT:
                stack_pointer = (stack_pointer + instruction.mov + buffer_size) % buffer_size;
                break;
        }

        program_counter++;
    }

    *cursor = stack_pointer;
}

void run_interpreter(Instruction* instruction_buffer, size_t no_instructions)
{
    size_t cursor = 0;
    byte memory_buffer[BF_MEMORY_STRIP_SIZE];
    memset(&memory_buffer, 0, BF_MEMORY_STRIP_SIZE);
    interpret_bf(instruction_buffer, no_instructions, &memory_buffer[0], &cursor, BF_MEMORY_STRIP_SIZE);
    putchar('\n');
}

void run_compiler(Instruction* instruction_buffer, size_t no_instructions, const char* file_name)
{
    BF_UNUSED(instruction_buffer); 
    BF_UNUSED(no_instructions); 
    BF_UNUSED(file_name);

    fprintf(stderr, "ERROR: Compiler not implemented\n");
    exit(EXIT_FAILURE);
}

void fprintf_indent(FILE* file, int indent_depth, const char* fmt, ...)
{
    while (indent_depth-- > 0) fprintf(file, "\t");

    va_list args;
    va_start(args, fmt);
    vfprintf(file, fmt, args);
    va_end(args);
}

int find_program(const char* compiler_command) 
{
    FILE* fp = popen(compiler_command, "r");
    if (fp == NULL) 
    {
        return 0;
    }
    pclose(fp);
    return 1;
}

void exec_bf(char* file_name) 
{
    char command[256];
    
    if (find_program(FIND_COMMAND("gcc"))) {
        BF_WRITE_COMMAND_BUF(command, BF_GCC_COMPILE_FMT(file_name));
    } else if (find_program(FIND_COMMAND("clang"))) {
        BF_WRITE_COMMAND_BUF(command, BF_CLANG_COMPILE_FMT(file_name));
    } else if (find_program(FIND_COMMAND("cl"))) {
        BF_WRITE_COMMAND_BUF(command, BF_MSVC_COMPILE_FMT(file_name));
    } else if (find_program(FIND_COMMAND("cc"))) {
        BF_WRITE_COMMAND_BUF(command, BF_CC_COMPILE_FMT(file_name));
    } else {
        fprintf(stderr, "WARNING: No C compiler found on system\n");
        return;
    }
    
    system(command);
}

void run_transpiler(Instruction* instruction_buffer, size_t no_instructions, const char* file_name)
{
    char* file_name_dot_c = strdup_dot_x(file_name, 'c');    
    FILE* file_c = fopen(file_name_dot_c, "w");
    free(file_name_dot_c);

    int indent_depth = 1;

    // Preprocessor and aliases
    fprintf(file_c, "#include <stdio.h>\n");
    fprintf(file_c, "#include <string.h>\n\n");
    fprintf(file_c, "#define MEMORY_STRIP_SIZE %d\n", BF_MEMORY_STRIP_SIZE);
    fprintf(file_c, "#define sp_mov(sp, inc) sp = (sp + MEMORY_STRIP_SIZE + inc) %% MEMORY_STRIP_SIZE\n\n");
    fprintf(file_c, "typedef char byte;\n\n");

    // Start of main
    fprintf(file_c, "int main()\n{\n");

    fprintf_indent(file_c, indent_depth, "byte mbuff[MEMORY_STRIP_SIZE];\n");
    fprintf_indent(file_c, indent_depth, "memset(&mbuff, 0, MEMORY_STRIP_SIZE);\n\n");

    fprintf_indent(file_c, indent_depth, "size_t sp = 0;\n\n");

    for (size_t program_counter = 0; program_counter < no_instructions; program_counter++)
    {
        Instruction instruction = instruction_buffer[program_counter];
        
        switch (instruction._op)
        {
            case OP_WRITE:
                fprintf_indent(file_c, indent_depth, "mbuff[sp]+=%ld;\n", instruction.inc);
                break;
            case OP_BRACKET_OPEN:
                fprintf_indent(file_c, indent_depth++, "while(mbuff[sp]) {\n");
                break;
            case OP_BRACKET_CLOSED:
                fprintf_indent(file_c, --indent_depth, "}\n");
                break;
            case OP_PRINT:
                fprintf_indent(file_c, indent_depth, "putchar(mbuff[sp]);\n");
                break;
            case OP_SHIFT:
                fprintf_indent(file_c, indent_depth, "sp_mov(sp,%ld);\n", instruction.inc);
                break;
        }
    }

    fprintf_indent(file_c, indent_depth, "putchar('\\n');\n");

    // End of main
    fprintf(file_c, "}\n");
    fclose(file_c);

    // Run compilation
    char* file_name_strip = strdup_dot_x_strip(file_name);
    exec_bf(file_name_strip);
    free(file_name_strip);
}

char* read_file_content(const char* filename) 
{

    FILE *file = fopen(filename, "rb");
    if (!file) 
    {
        fprintf(stderr, "ERROR: Unable to read `%s`\n", filename);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    size_t length = ftell(file);
    rewind(file);

    char *file_cstr = malloc(length + 1);
    BF_MEM_ASSERT(file_cstr);

    size_t bytesRead = fread(file_cstr, sizeof(char), length, file);
    if (bytesRead != length) 
    {
        fprintf(stderr, "ERROR: File read mismatch `%s` (%zu/%zu bytes)\n", filename, bytesRead, length);
        free(file_cstr);
        fclose(file);
        return NULL;
    }
    
    file_cstr[length] = '\0';

    fclose(file);
    return file_cstr;
}

void run_interactive()
{
    char input[BF_INTERACTIVE_INPUT_SIZE + 1];

    size_t cursor = 0;
    byte memory_buffer[BF_MEMORY_STRIP_SIZE];
    memset(&memory_buffer, 0, BF_MEMORY_STRIP_SIZE);

    while(1)
    {
        printf("bf$ ");
        fgets(input, sizeof(input), stdin);

        // Input is too large
        if (strchr(input, '\n') == NULL) 
        {
            size_t input_size = BF_INTERACTIVE_INPUT_SIZE;
            while (getchar() != '\n') input_size++;
            printf("ERROR: Input %zu characters (max %d)\n", input_size, BF_INTERACTIVE_INPUT_SIZE);
            return;
        }
        else if (input[0] == 'x' || input[0] == 'X') // Quit COMMAND
        {
            return;
        }
        else if (input[0] == '!') // Dump memory COMMAND
        {
            for (size_t i = 0; i < BF_MEMORY_STRIP_SIZE; i++)
            {
                if (memory_buffer[i]) 
                    putchar(memory_buffer[i]);
            }
        }
        else if (input[0] == 'r' || input[0] == 'R') // Erase memory COMMAND
        {
            memset(&memory_buffer, 0, BF_MEMORY_STRIP_SIZE);
        }
        else if (input[0] == 's' || input[0] == 'S') // Reset cursor COMMAND
        {
            cursor = 0;
        }
        else if (input[0] == ',') // Write to cursor COMMAND
        {
            memory_buffer[cursor] = input[1] == '\n' ? '\0' : input[1];
        }
        else if (input[0] == 'p' || input[0] == 'P')
        {
            printf("%zu:%d", cursor, BF_MEMORY_STRIP_SIZE);
        }
        else // Interpret 
        {
            size_t nb_instr;
            Instruction* instruction_buffer = parse_instructions(input, &nb_instr);
            if (instruction_buffer) {
                interpret_bf(instruction_buffer, nb_instr, &memory_buffer[0], &cursor, BF_MEMORY_STRIP_SIZE);
                free(instruction_buffer);
            }
        }

        printf("\n");
    }


}


int main(int argc, char *argv[])
{
    if (argc == 1) 
    {
        run_interactive();
        exit(EXIT_SUCCESS);
    }

    Option option;
    const char* file_name;

    get_program_info(argc, argv, &option, &file_name);
    char* program_content = read_file_content(file_name);
    if (!program_content) exit(EXIT_FAILURE);

    size_t nb_instructions;
    Instruction* instruction_buffer = parse_instructions(program_content, &nb_instructions);
    if (!instruction_buffer) return EXIT_FAILURE;
    
    free(program_content);

    switch (option)
    {
        case OPTION_INTERPRET: 
            run_interpreter(instruction_buffer, nb_instructions);
            break;
        case OPTION_COMPILE:
            run_compiler(instruction_buffer, nb_instructions, file_name);
            break;
        case OPTION_TRANSPILE: 
            run_transpiler(instruction_buffer, nb_instructions, file_name);
            break;
    }

    free(instruction_buffer);
}
#include <avr/io.h>

/*
    This program implements concepts taken from the game _Human Resource Machine_. This game implements a simple language similar to a microprocessor
    assembly language, but with a few odd features that aren't typical for a microprocessor.

    - "The avatar's hands" act as a sort of accumulator register. Most instructions operate on this register. This is similar to the accumulator
      in a 6502, except that it has an distinguishable "empty" state, so it is perhaps better thought of as a single-element queue. We will implement
      it as a signed 16-bit integer.

    - Currently I am implemented "types" by using type flags, but we could use less storage if we implemented special value ranges to indicate special
      values and types. We could use -32768 (0x8000) to indicate "empty." The legal range of a number is -999 to +999, so we could encode those values
      directly. The game also sometimes processes character values which are a distinct type. These seem to be only uppercase alphabetic ASCII. We
      could encode these in the special range 1000 to 1025. To convert from ASCII, add 937, and to convert back to ASCII, subtract 937. The game also
      allows indirect memory addressing, so we could implement addresses as a separate type, and jumps to addresses, although in the game the
      addresses are indicated visually and not entered as numbers (but instructions are given numbers, so you can see where the jump is going).

    - For now I am implementing the different types with enumerations which uses more space but makes the code easier to read.

    - Terminology (and names) are a bit confusing if you are an actual programmer; the "inbox" is really an input queue; the "outbox" is an output
      queue; the way the instructions are labeled is backwards (INBOX reads FROM the inbox and puts its value in your "hands," but the graphical
      instruction is labeled with an arrow pointing to the word "inbox," while OUTBOX is labeled with the word "outbox" and an arrow pointing away
      from it).

    - I'm not sure, but it may be illegal to "bump" or do subtraction and addition on character values (test this).

    - Some problems give us a small amount of memory; some give us no memory. Memory is represented in the game as a rectangular set of numbered
      squares on the floor. Memory locations is indexed starting from zero. I assume that the "hardware" throws an error if the program tries to
      access a non-existent memory location. Memory locations may also be empty. Reading from an empty location produces an immediate error. When I
      implement memory, do it by adding a "room_floor" defined for each room which indicates the dimensions of the memory. In some rooms, values
      in memory are supplied with initial values like zero and one for convenience. I need to be able to specify initial non-empty values for some
      memory locations in a given room.

    The basic instructions are:

    INBOX
    
    Read from an input FIFO (first in, first out) queue. In the game, executing this instruction when the queue is empty will immediately terminate
    the program and in fact this is the normal way to terminate programs: a program typically executes an INBOX instruction, processes the item
    received, and then loops back to the top to execute INBOX again until the input queue is empty. To implement an interactive (non-batch mode)
    version of the runtime, make INBOX block on receiving a value.

    OUTBOX
    
    Write to an output FIFO queue. If a program deviates from the expected output, the "boss" will terminate it immediately, so I haven't been able
    to determine an actual limit to the number of values that can be placed in in the output FIFO. Executing OUTBOX with "empty hands" also causes an
    immediate error.

    COPYFROM
    
    Copies from a memory location to your "hands." Generates an immediate error if the memory location specified is invalid for the room, or is empty.

    COPYTO: copies from your "hands" to a memory location. Generates an immediate error if your hands are empty.

    In the visual version of the game, it is impossible to specify an invalid memory address, because you write the program by choosing a space on the
    room floor. However, a text representation of the program could specify an invalid memory address, so we should check these against the room
    floor dimensions.

    COPYTO and COPYFROM have "indirect" variations which read a value from a memory location, then use that value as the memory address. These also
    have to be checked, both the direct address and the resulting address when following the indirection.

    ADD: copies a value from a memory location and adds it to the value in your "hands," leaving the sum in your hands. Generates an immediate error
    if either the memory location or your "hands" are empty. Add always acts as if the numbers are signed. I think it is illegal to mix operations --
    that is, you can't add a character to a number or vice-versa. I'm not sure what happens if you add to a character and generate an invalid
    character.

    SUB: similar to "add." Always acts as if the numbers are signed. The only way I know to determine if two values are equal is to subtract them
    and use "jump if zero" to determine if the result is zero. I know this can be done on a character but I'm not sure what happens if you subtract
    and generate an invalid character.

    ADD and SUB have "indirect" variations which presumably also must have their addresses checked.

    JUMP: unconditional jump to a new program location. In the game, programs use "destination" placeholders. These are not actual instructions and
    don't count towards the program's instruction count or execution time count. I implement these as 1-based instruction addresses because that's the
    way the programs are displayed in the game, but for implementation we convert these to zero-based array indices.

    JUMP_IF_ZERO: conditional jump; jumps if the value in your "hands" is zero. If the value isn't zero, your program continues with the next
    instruction. Works on either numbers or characters.

    JUMP_IF_NEGATIVE: similar to jump if zero, except it checks for a negative value in your "hands." I'm not sure what happens if you try to
    JUMP_IF_NEGATIVE on a character value.

    BUMP_PLUS: increments the value in a memory location. Exceeding 999 generates an immediate error. Copies the incremented value into your "hands."
    I'm not sure if you can bump up a character. Maybe you can bump up a character as long as the result doesn't exceed the range A..Z.

    BUMP_MINUS: similar to BUMP_PLUS, but decrements, and you can't make a value less than -999. I'm not sure if you can bump down a character or what
    happens if you exceed the A..Z range.

    BUMP_PLUS and BUMP_MINUS have "indirect" variations.

    As far as I can tell, all "instructions" execute in one "clock cycle" (your program is evaluated on its instruction length and number of "clock
    cycles" spent executing, which I think is equivalent to the number of instructions executed.

    That's the entire instruction set! But it's enough to execute any of the program challenges, including complex sorting and searching algorithms,
    implementing linked lists in arrays, and other basic concepts from programming classes.
*/

typedef enum HRMValueType_e
{
    EMPTY,
    NO_PARAM = EMPTY,
    CHAR,
    NUM,
    MEM_ADDR,
    PROG_ADDR /* Used by jumps; one-based program instruction index */

} HRMValueType_t;

typedef int16_t hrm_num;

typedef uint8_t hrm_char;

typedef union HRMVal_u
{
    hrm_num n;
    hrm_char c;

} HRMVal_u_t;

typedef struct HRMVal_s
{
    HRMValueType_t type;
    HRMVal_u_t val;

} HRMVal_t;

#define NUM_FLOOR_VALUES ( 9 )
#define NUM_INBOX_VALUES ( 8 )

static HRMVal_t floor_a[NUM_FLOOR_VALUES];

/* Sample input data from the game */
static HRMVal_t in_fifo[NUM_INBOX_VALUES] = { { NUM, .val.n =  7  },
                                              { NUM, .val.n =  0  },
                                              { NUM, .val.n =  5  },
                                              { NUM, .val.c = 'D' },
                                              { NUM, .val.n =  0  },
                                              { NUM, .val.n =  0  },
                                              { NUM, .val.n =  0  },
                                              { NUM, .val.n =  0  } };

/*
    For now, assume the out FIFO won't have more values than the in, although this is not true for all the programs we want to implement
*/
static HRMVal_t out_fifo[NUM_INBOX_VALUES];

typedef enum HRMInstructionType_e
{
    INBOX,
    OUTBOX,
    COPYFROM,
    COPYFROM_IND,
    COPYTO,
    COPYTO_IND,
    ADD,
    ADD_IND,
    SUB,
    SUB_IND,
    BUMP_PLUS,
    BUMP_PLUS_IND,
    BUMP_MINUS,
    BUMP_MINUS_IND,
    JUMP,
    JUMP_ZERO,
    JUMP_NEGATIVE

} HRMInstructionType_t;

typedef struct HRMInst_s
{
    HRMInstructionType_t inst;
    HRMVal_t param;

} HRMInstruction_t;

typedef enum HRMErr_e
{
    ERR_NONE = 0,
    ERR_BAD_PARAM_TYPE,
    ERR_EMPTY_HANDS,
    ERR_INVALID_TYPE_FOR_DIRECT_ADDR,
    ERR_DIRECT_ADDR_OUT_OF_RANGE,
    ERR_INVALID_TYPE_FOR_INDIRECT_ADDR,
    ERR_INDIRECT_ADDR_OUT_OF_RANGE,
    ERR_COPYFROM_READING_EMPTY_ADDR,
    ERR_COPYFROM_IND_READING_EMPTY_ADDR,
    ERR_BAD_ADDEND_TYPE_IN_HANDS,
    ERR_BAD_SUBTRAHEND_TYPE_IN_HANDS,
    ERR_BAD_ADDEND_TYPE_IN_MEMORY,
    ERR_BAD_SUBTRAHEND_TYPE_IN_MEMORY,
    ERR_BAD_TYPE_FOR_BUMP_IN_MEMORY,
    ERR_OVERFLOW,
    ERR_UNDERFLOW,
    
} HRMErr_t;

/*
    Note that in the game, program addresses are 1-based, so we encode them that way.
*/
#define ROOM_MEMORY_SIZE_ZERO_PRESERVATION_INITIATIVE ( 9 )
static HRMVal_t mem_zero_preservation_initiative[ROOM_MEMORY_SIZE_ZERO_PRESERVATION_INITIATIVE] = { 0 };
static const HRMInstruction_t pgm_zero_preservation_initiative[] = {
                                                                     { INBOX                                  }, /* 1 */
                                                                     { JUMP_ZERO, { PROG_ADDR,   { .n = 4 } } }, /* 2 */
                                                                     { JUMP,      { PROG_ADDR,   { .n = 1 } } }, /* 3 */
                                                                     { OUTBOX                                 }, /* 4 */
                                                                     { JUMP,      { PROG_ADDR,   { .n = 1 } } }, /* 5 */
                                                                 };

#define MAX_INSTRUCTIONS_ALLOWED ( 1000 )

static HRMVal_t hands = { EMPTY, {} };


static HRMErr_t verify_hands_not_empty( void )
{
    HRMErr_t ret_val = ERR_NONE;
    if ( EMPTY == hands.type )
    {
        ret_val = ERR_EMPTY_HANDS;
    }

    return ret_val;

}


/*
    Check that the hands hold a value which can be legitimately added, subtracted, or bumped
*/
static HRMErr_t verify_hands_hold_number( void )
{
    HRMErr_t ret_val = verify_hands_not_empty();
    if ( ( ERR_NONE == ret_val ) && ( NUM != hands.type ) )
    {
        ret_val = ERR_BAD_ADDEND_TYPE_IN_HANDS;
    }

    return ret_val;

}


static HRMErr_t verify_direct_addr( HRMVal_t direct_addr, HRMVal_t * const mem, uint8_t const mem_len )
{
    HRMErr_t ret_val = ERR_NONE;
    if ( NUM != direct_addr.type )
    {
        ret_val = ERR_INVALID_TYPE_FOR_DIRECT_ADDR;
    }
    else if ( direct_addr.val.n >= ( uint16_t )mem_len )
    {
        ret_val = ERR_DIRECT_ADDR_OUT_OF_RANGE;
    }

    return ret_val;

}


static HRMErr_t verify_indirect_addr( HRMVal_t indirect_addr, HRMVal_t * const mem, uint8_t const mem_len )
{
    HRMErr_t ret_val = ERR_NONE;
    if ( NUM != indirect_addr.type )
    {
        ret_val = ERR_INVALID_TYPE_FOR_INDIRECT_ADDR;
    }
    else if ( indirect_addr.val.n >= ( uint16_t )mem_len )
    {
        ret_val = ERR_INDIRECT_ADDR_OUT_OF_RANGE;
    }
    else
    {
        ret_val = verify_direct_addr( mem[indirect_addr.val.n], mem, mem_len );
    }

    return ret_val;

}


static HRMErr_t execute( HRMInstruction_t const * const pgm, uint8_t const pgm_len, HRMVal_t * const mem, uint8_t const mem_len )
{
    uint16_t pgm_num_instructions_executed = 0;
    uint16_t pgm_pc = 0;

    uint8_t inbox_empty = 0;

    uint8_t in_fifo_val_idx = 0;
    uint8_t out_fifo_num_vals = 0;

    HRMErr_t err = ERR_NONE;
    HRMVal_t value;

    /* Our "virtual machine" */
    while ( ( 0 == inbox_empty )          &&
            ( ERR_NONE == err )           &&
            ( pgm_pc <= ( pgm_len - 1 ) ) &&
            ( pgm_num_instructions_executed <= MAX_INSTRUCTIONS_ALLOWED ) )
    {
        switch ( pgm[pgm_pc].inst )
        {
            case INBOX:
                if ( in_fifo_val_idx >= NUM_INBOX_VALUES )
                {
                    inbox_empty = 1;
                }
                else
                {
                    hands = in_fifo[in_fifo_val_idx];
                    in_fifo_val_idx += 1;
                    pgm_pc += 1;
                }
                pgm_num_instructions_executed += 1;
                break;

            case OUTBOX:
                if ( EMPTY == hands.type )
                {
                    err = ERR_EMPTY_HANDS;
                }
                else
                {
                    out_fifo[out_fifo_num_vals] = hands;
                    out_fifo_num_vals += 1;
                    pgm_pc += 1;
                }
                pgm_num_instructions_executed += 1;
                break;

            case COPYFROM:
                err = verify_direct_addr( pgm[pgm_pc].param, mem, mem_len );
                if ( ERR_NONE == err )
                {
                    value = mem[pgm[pgm_pc].param.val.n];
                    if ( EMPTY == value.type )
                    {
                        err = ERR_COPYFROM_READING_EMPTY_ADDR;
                    }
                    else
                    {
                        hands = value;
                    }
                }
                break;

            case COPYFROM_IND:
                err = verify_indirect_addr( pgm[pgm_pc].param, mem, mem_len );
                if ( ERR_NONE == err )
                {
                    value = mem[mem[pgm[pgm_pc].param.val.n].val.n];
                    if ( EMPTY == value.type )
                    {
                        err = ERR_COPYFROM_IND_READING_EMPTY_ADDR;
                    }
                    else
                    {
                        hands = value;
                    }
                }
                break;

            case COPYTO:
                err = verify_direct_addr( pgm[pgm_pc].param, mem, mem_len );
                if ( ERR_NONE == err )
                {
                    mem[pgm[pgm_pc].param.val.n] = hands;
                    hands.type = EMPTY;
                }
                break;

            case COPYTO_IND:
                err = verify_indirect_addr( pgm[pgm_pc].param, mem, mem_len );
                if ( ERR_NONE == err )
                {
                    mem[mem[pgm[pgm_pc].param.val.n].val.n] = hands;
                    hands.type = EMPTY;
                }
                break;

            case ADD:
                if ( ERR_NONE != verify_hands_hold_number() )
                {
                    err = ERR_BAD_ADDEND_TYPE_IN_HANDS;
                }
                else
                {
                    err = verify_direct_addr( pgm[pgm_pc].param, mem, mem_len );
                    if ( ERR_NONE == err )
                    {
                        value = mem[pgm[pgm_pc].param.val.n];
                        if ( value.type != NUM )
                        {
                            err = ERR_BAD_ADDEND_TYPE_IN_MEMORY;
                        }
                        else
                        {
                            hands.val.n += value.val.n;
                            if ( hands.val.n < 999 )
                            {
                                err = ERR_UNDERFLOW;
                            }
                            else if ( hands.val.n > 999 )
                            {
                                err = ERR_OVERFLOW;
                            }
                        }
                    }
                }
                break;

            case ADD_IND:
                if ( ERR_NONE != verify_hands_hold_number() )
                {
                    err = ERR_BAD_ADDEND_TYPE_IN_HANDS;
                }
                else
                {
                    err = verify_indirect_addr( pgm[pgm_pc].param, mem, mem_len );
                    if ( ERR_NONE == err )
                    {
                        value = mem[mem[pgm[pgm_pc].param.val.n].val.n];
                        if ( value.type != NUM )
                        {
                            err = ERR_BAD_ADDEND_TYPE_IN_MEMORY;
                        }
                        else
                        {
                            hands.val.n += value.val.n;
                            if ( hands.val.n < 999 )
                            {
                                err = ERR_UNDERFLOW;
                            }
                            else if ( hands.val.n > 999 )
                            {
                                err = ERR_OVERFLOW;
                            }
                        }
                    }
                }
                break;

            case SUB:
                if ( ERR_NONE != verify_hands_hold_number() )
                {
                    err = ERR_BAD_SUBTRAHEND_TYPE_IN_HANDS;
                }
                else
                {
                    err = verify_direct_addr( pgm[pgm_pc].param, mem, mem_len );
                    if ( ERR_NONE == err )
                    {
                        value = mem[pgm[pgm_pc].param.val.n];
                        if ( value.type != NUM )
                        {
                            err = ERR_BAD_SUBTRAHEND_TYPE_IN_MEMORY;
                        }
                        else
                        {
                            hands.val.n -= value.val.n;
                            if ( hands.val.n < 999 )
                            {
                                err = ERR_UNDERFLOW;
                            }
                            else if ( hands.val.n > 999 )
                            {
                                err = ERR_OVERFLOW;
                            }
                        }
                    }
                }
                break;

            case SUB_IND:
                if ( ERR_NONE != verify_hands_hold_number() )
                {
                    err = ERR_BAD_SUBTRAHEND_TYPE_IN_HANDS;
                }
                else
                {
                    err = verify_indirect_addr( pgm[pgm_pc].param, mem, mem_len );
                    if ( ERR_NONE == err )
                    {
                        value = mem[mem[pgm[pgm_pc].param.val.n].val.n];
                        if ( value.type != NUM )
                        {
                            err = ERR_BAD_SUBTRAHEND_TYPE_IN_MEMORY;
                        }
                        else
                        {
                            hands.val.n -= value.val.n;
                            if ( hands.val.n < 999 )
                            {
                                err = ERR_UNDERFLOW;
                            }
                            else if ( hands.val.n > 999 )
                            {
                                err = ERR_OVERFLOW;
                            }
                        }
                    }
                }
                break;

            case BUMP_PLUS:
                err = verify_direct_addr( pgm[pgm_pc].param, mem, mem_len );
                if ( ERR_NONE == err )
                {
                    value = mem[pgm[pgm_pc].param.val.n];
                    if ( value.type != NUM )
                    {
                        err = ERR_BAD_TYPE_FOR_BUMP_IN_MEMORY;
                    }
                    else
                    {
                        value.val.n += 1;
                        if ( value.val.n > 999 )
                        {
                            err = ERR_OVERFLOW;
                        }
                        else
                        {
                            mem[pgm[pgm_pc].param.val.n] = value;
                            hands = value;
                        }
                    }
                }
                break;

            case BUMP_PLUS_IND:
                err = verify_indirect_addr( pgm[pgm_pc].param, mem, mem_len );
                if ( ERR_NONE == err )
                {
                    value = mem[mem[pgm[pgm_pc].param.val.n].val.n];
                    if ( value.type != NUM )
                    {
                        err = ERR_BAD_TYPE_FOR_BUMP_IN_MEMORY;
                    }
                    else
                    {
                        value.val.n += 1;
                        if ( value.val.n > 999 )
                        {
                            err = ERR_OVERFLOW;
                        }
                        else
                        {
                            mem[mem[pgm[pgm_pc].param.val.n].val.n] = value;
                            hands = value;
                        }
                    }
                }
                break;

            case BUMP_MINUS:
                err = verify_direct_addr( pgm[pgm_pc].param, mem, mem_len );
                if ( ERR_NONE == err )
                {
                    value = mem[pgm[pgm_pc].param.val.n];
                    if ( value.type != NUM )
                    {
                        err = ERR_BAD_TYPE_FOR_BUMP_IN_MEMORY;
                    }
                    else
                    {
                        value.val.n -= 1;
                        if ( value.val.n < 999 )
                        {
                            err = ERR_UNDERFLOW;
                        }
                        else
                        {
                            mem[pgm[pgm_pc].param.val.n] = value;
                            hands = value;
                        }
                    }
                }
                break;

            case BUMP_MINUS_IND:
                err = verify_indirect_addr( pgm[pgm_pc].param, mem, mem_len );
                if ( ERR_NONE == err )
                {
                    value = mem[mem[pgm[pgm_pc].param.val.n].val.n];
                    if ( value.type != NUM )
                    {
                        err = ERR_BAD_TYPE_FOR_BUMP_IN_MEMORY;
                    }
                    else
                    {
                        value.val.n -= 1;
                        if ( value.val.n < 999 )
                        {
                            err = ERR_UNDERFLOW;
                        }
                        else
                        {
                            mem[mem[pgm[pgm_pc].param.val.n].val.n] = value;
                            hands = value;
                        }
                    }
                }
                break;

            case JUMP:
                pgm_pc = pgm[pgm_pc].param.val.n - 1; /* Convert to zero-based */
                pgm_num_instructions_executed += 1;
                break;

            case JUMP_ZERO:
                if ( NUM != hands.type )
                {
                    err = 1;
                }
                else if ( 0 == hands.val.n )
                {
                    pgm_pc = pgm[pgm_pc].param.val.n - 1; /* Convert to zero-based */
                }
                else
                {
                    pgm_pc += 1;
                }
                pgm_num_instructions_executed += 1;
                break;

            case JUMP_NEGATIVE:
                if ( NUM != hands.type )
                {
                    err = 1;
                }
                else if ( hands.val.n < 0 )
                {
                    pgm_pc = pgm[pgm_pc].param.val.n - 1; /* Convert to zero-based */
                }
                else
                {
                    pgm_pc += 1;
                }
                pgm_num_instructions_executed += 1;
                break;

            default:
                err = 1;
                break;

        }

    }

    return err;

}

int main(void)
{
    uint8_t err = execute( pgm_zero_preservation_initiative, ( uint8_t )( sizeof( pgm_zero_preservation_initiative ) / sizeof( HRMInstruction_t ) ),
                           mem_zero_preservation_initiative, ( uint8_t )ROOM_MEMORY_SIZE_ZERO_PRESERVATION_INITIATIVE );

    return ( int )err;

}

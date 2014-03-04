/*
 * Copyright (C) 2002-2015, Parrot Foundation.
 */

#include <stdlib.h>
#include <string.h>
#define _PARSER
#include "imc.h"
#include "pbc.h"
#include "optimizer.h"
#include "pmc/pmc_callcontext.h"
#include "parrot/oplib/core_ops.h"

/*

=head1 NAME

compilers/imcc/instructions.c

=head1 DESCRIPTION

When generating the code, the instructions of the program
are stored in an array.

After the register allocation is resolved, the instructions
array is flushed.

These functions operate over this array and its contents.

=head2 Functions

=over 4

=cut

*/

/* HEADERIZER HFILE: compilers/imcc/instructions.h */

/* HEADERIZER BEGIN: static */
/* Don't modify between HEADERIZER BEGIN / HEADERIZER END.  Your changes will be lost. */

static int e_pasm_open(ARGMOD(imc_info_t * imcc), ARGIN(STRING *path))
        __attribute__nonnull__(1)
        __attribute__nonnull__(2)
        FUNC_MODIFIES(* imcc);

#define ASSERT_ARGS_e_pasm_open __attribute__unused__ int _ASSERT_ARGS_CHECK = (\
       PARROT_ASSERT_ARG(imcc) \
    , PARROT_ASSERT_ARG(path))
/* Don't modify between HEADERIZER BEGIN / HEADERIZER END.  Your changes will be lost. */
/* HEADERIZER END: static */

static const char types[] = "INPS";

/*

=item C<Instruction * _mk_instruction(const char *op, const char *fmt, int n,
SymReg * const *r, int flags)>

Creates a new instruction

=cut

*/

PARROT_MALLOC
PARROT_CANNOT_RETURN_NULL
Instruction *
_mk_instruction(ARGIN(const char *op), ARGIN(const char *fmt), int n,
        ARGIN(SymReg * const *r), int flags)
{
    ASSERT_ARGS(_mk_instruction)
    const size_t reg_space  = (n > 1) ? (sizeof (SymReg *) * (n - 1)) : 0;
    Instruction * const ins =
        (Instruction*)mem_sys_allocate_zeroed(sizeof (Instruction) + reg_space);
    int i;

    ins->opname       = mem_sys_strdup(op);
    ins->format       = mem_sys_strdup(fmt);
    ins->symreg_count = n;

    for (i = 0; i < n; i++)
        ins->symregs[i]  = r[i];

    ins->flags = flags;
    ins->op    = NULL;

    return ins;
}

/*

=item C<int instruction_reads(const Instruction *ins, const SymReg *r)>

next two functions are called very often, says gprof
they should be fast

=cut

*/

int
instruction_reads(ARGIN(const Instruction *ins), ARGIN(const SymReg *r))
{
    ASSERT_ARGS(instruction_reads)
    int f, i;
    op_lib_t *core_ops = PARROT_GET_CORE_OPLIB(NULL);

    if (ins->op && ins->op->lib == core_ops) {
        if (OP_INFO_OPNUM(ins->op) == PARROT_OP_set_args_pc
        ||  OP_INFO_OPNUM(ins->op) == PARROT_OP_set_returns_pc) {

            for (i = ins->symreg_count - 1; i >= 0; --i)
                if (r == ins->symregs[i])
                    return 1;

            return 0;
        }
        else if (OP_INFO_OPNUM(ins->op) == PARROT_OP_get_params_pc ||
                 OP_INFO_OPNUM(ins->op) == PARROT_OP_get_results_pc) {
            return 0;
        }
    }

    f = ins->flags;

    for (i = ins->symreg_count - 1; i >= 0; --i) {
        if (f & (1 << i)) {
            const SymReg * const ri = ins->symregs[i];

            if (ri == r)
                return 1;

            /* this additional test for _kc ops seems to slow
             * down instruction_reads by a huge amount compared to the
             * _writes below
             */
            if (ri->set == 'K') {
                const SymReg *key;
                for (key = ri->nextkey; key; key = key->nextkey)
                    if (key->reg == r)
                        return 1;
            }
        }
    }

    /* a sub call reads the previous args */
    if (ins->type & ITPCCSUB) {
        while (ins && ins->op != &core_ops->op_info_table[PARROT_OP_set_args_pc])
            ins = ins->prev;

        if (!ins)
            return 0;

        for (i = ins->symreg_count - 1; i >= 0; --i) {
            if (ins->symregs[i] == r)
                return 1;
        }
    }

    return 0;
}

/*

=item C<int instruction_writes(const Instruction *ins, const SymReg *r)>

Determines whether the instruction C<ins> writes to the SymReg C<r>.
Returns 1 if it does, 0 if not.

=cut

*/

int
instruction_writes(ARGIN(const Instruction *ins), ARGIN(const SymReg *r))
{
    ASSERT_ARGS(instruction_writes)
    const int f = ins->flags;
    int j;
    op_lib_t *core_ops = PARROT_GET_CORE_OPLIB(NULL);

    /* a get_results opcode occurs after the actual sub call */
    if (ins->op == &core_ops->op_info_table[PARROT_OP_get_results_pc]) {
        int i;
        /* Old assumption: But only if it isn't the get_results opcode
         * of an ExceptionHandler, which doesn't have a call next.
         *
         * Not with GH #1041: invokecc vs exception handler:
         * invokecc + get_results x, $I0 does write to $I0
        if (ins->prev && (ins->prev->type & ITPCCSUB))
            return 0;
         */

        for (i = ins->symreg_count - 1; i >= 0; --i) {
            if (ins->symregs[i] == r)
                return 1;
        }

        return 0;
    }
    else if (ins->type & ITPCCSUB) {
        int i;
        ins = ins->prev;
        /* can't used pcc_sub->ret due to bug #38406
         * it seems that all sub SymRegs are shared
         * and point to the most recent pcc_sub
         * structure
         */
        while (ins && ins->op != &core_ops->op_info_table[PARROT_OP_get_results_pc])
            ins = ins->next;

        if (!ins)
            return 0;

        for (i = ins->symreg_count - 1; i >= 0; --i) {
            if (ins->symregs[i] == r)
                return 1;
        }

        return 0;
    }

    if (ins->op == &core_ops->op_info_table[PARROT_OP_get_params_pc]) {
        int i;

        for (i = ins->symreg_count - 1; i >= 0; --i) {
            if (ins->symregs[i] == r)
                return 1;
        }

        return 0;
    }
    else if (ins->op == &core_ops->op_info_table[PARROT_OP_set_args_pc]
         ||  ins->op == &core_ops->op_info_table[PARROT_OP_set_returns_pc]) {
        return 0;
    }

    for (j = 0; j < ins->symreg_count; j++)
        if (f & (1 << (16 + j)))
            if (ins->symregs[j] == r)
                return 1;

    return 0;
}


/*

=item C<int get_branch_regno(const Instruction *ins)>

Get the register number of an address which is a branch target

=cut

*/

int
get_branch_regno(ARGIN(const Instruction *ins))
{
    ASSERT_ARGS(get_branch_regno)
    int j;

    for (j = ins->opsize - 2; j >= 0 && ins->symregs[j] ; --j)
        if (ins->type & (1 << j))
            return j;

    return -1;
}

/*

=item C<SymReg * get_branch_reg(const Instruction *ins)>

Get the register corresponding to an address which is a branch target

=cut

*/

PARROT_WARN_UNUSED_RESULT
PARROT_CAN_RETURN_NULL
SymReg *
get_branch_reg(ARGIN(const Instruction *ins))
{
    ASSERT_ARGS(get_branch_reg)
    const int r = get_branch_regno(ins);

    if (r >= 0)
        return ins->symregs[r];

    return NULL;
}

/* some useful instruction routines */

/*

=item C<Instruction * _delete_ins(IMC_Unit *unit, Instruction *ins)>

Delete instruction ins. It's up to the caller to actually free the memory
of ins, if appropriate.

The instruction following ins is returned.

=cut

*/

PARROT_WARN_UNUSED_RESULT
PARROT_CAN_RETURN_NULL
Instruction *
_delete_ins(ARGMOD(IMC_Unit *unit), ARGIN(Instruction *ins))
{
    ASSERT_ARGS(_delete_ins)
    Instruction * const next = ins->next;
    Instruction * const prev = ins->prev;

    if (prev)
        prev->next = next;
    else
        unit->instructions = next;

    if (next)
        next->prev = prev;
    else
        unit->last_ins = prev;

    return next;
}

/*

=item C<Instruction * delete_ins(IMC_Unit *unit, Instruction *ins)>

Delete instruction ins, and then free it.

The instruction following ins is returned.

=cut

*/

PARROT_WARN_UNUSED_RESULT
PARROT_CAN_RETURN_NULL
Instruction *
delete_ins(ARGMOD(IMC_Unit *unit), ARGMOD(Instruction *ins))
{
    ASSERT_ARGS(delete_ins)
    Instruction * next = _delete_ins(unit, ins);


    free_ins(ins);

    return next;
}

/*

=item C<void insert_ins(IMC_Unit *unit, Instruction *ins, Instruction *tmp)>

Insert Instruction C<tmp> in the execution flow after Instruction
C<ins>.

=cut

*/

void
insert_ins(ARGMOD(IMC_Unit *unit), ARGMOD_NULLOK(Instruction *ins),
        ARGMOD(Instruction *tmp))
{
    ASSERT_ARGS(insert_ins)
    if (!ins) {
        Instruction * const next = unit->instructions;

        unit->instructions = tmp;
        tmp->next          = next;

        if (next) {
            next->prev = tmp;
            tmp->line  = next->line;
        }
        else {
            unit->last_ins = tmp;
        }
    }
    else {
        Instruction * const next = ins->next;

        ins->next = tmp;
        tmp->prev = ins;
        tmp->next = next;

        if (next)
            next->prev = tmp;
        else
            unit->last_ins = tmp;

        if (!tmp->line)
            tmp->line = ins->line;
    }
}

/*

=item C<void prepend_ins(IMC_Unit *unit, Instruction *ins, Instruction *tmp)>

Insert Instruction C<tmp> into the execution flow before
Instruction C<ins>.

=cut

*/

void
prepend_ins(ARGMOD(IMC_Unit *unit), ARGMOD_NULLOK(Instruction *ins),
        ARGMOD(Instruction *tmp))
{
    ASSERT_ARGS(prepend_ins)
    if (!ins) {
        Instruction * const next = unit->instructions;

        unit->instructions = tmp;
        tmp->next          = next;
        next->prev         = tmp;
        tmp->line          = next->line;
    }
    else {
        Instruction * const prev = ins->prev;

        ins->prev = tmp;
        tmp->next = ins;
        tmp->prev = prev;

        if (prev)
            prev->next = tmp;

        if (!tmp->line)
            tmp->line = ins->line;
    }
}

/*

=item C<void subst_ins(IMC_Unit *unit, Instruction *ins, Instruction *tmp, int
needs_freeing)>

Substitute Instruction C<tmp> for Instruction C<ins>.
Free C<ins> if C<needs_freeing> is true.

=cut

*/

void
subst_ins(ARGMOD(IMC_Unit *unit), ARGMOD(Instruction *ins),
          ARGMOD(Instruction *tmp), int needs_freeing)
{
    ASSERT_ARGS(subst_ins)
    Instruction * const prev = ins->prev;


    if (prev)
        prev->next = tmp;
    else
        unit->instructions = tmp;

    tmp->prev = prev;
    tmp->next = ins->next;

    if (ins->next)
        ins->next->prev = tmp;
    else
        unit->last_ins = tmp;

    if (tmp->line == 0)
        tmp->line = ins->line;

    if (needs_freeing)
        free_ins(ins);
}

/*

=item C<Instruction * move_ins(IMC_Unit *unit, Instruction *ins, Instruction
*to)>

Move instruction ins from its current position to the position
following instruction to. Returns the instruction following the
initial position of ins.

=cut

*/

PARROT_CAN_RETURN_NULL
Instruction *
move_ins(ARGMOD(IMC_Unit *unit), ARGMOD(Instruction *ins), ARGMOD(Instruction *to))
{
    ASSERT_ARGS(move_ins)
    Instruction * const next = _delete_ins(unit, ins);
    insert_ins(unit, to, ins);
    return next;
}


/*

=item C<Instruction * emitb(imc_info_t * imcc, IMC_Unit *unit, Instruction *i)>

Emit a single instruction into the current unit buffer.

=cut

*/

PARROT_CAN_RETURN_NULL
Instruction *
emitb(ARGMOD(imc_info_t * imcc), ARGMOD_NULLOK(IMC_Unit *unit),
        ARGIN_NULLOK(Instruction *i))
{
    ASSERT_ARGS(emitb)
    if (!unit || !i)
        return NULL;

    if (!unit->instructions)
        unit->last_ins = unit->instructions = i;
    else {
        unit->last_ins->next = i;
        i->prev              = unit->last_ins;
        unit->last_ins       = i;
    }

    /* lexer is in next line already */
    i->line = imcc->line;

    return i;
}

/*

=item C<void free_ins(Instruction *ins)>

Free the Instruction structure ins.

=cut

*/

void
free_ins(ARGMOD(Instruction *ins))
{
    ASSERT_ARGS(free_ins)
    mem_sys_free(ins->format);
    mem_sys_free(ins->opname);
    mem_sys_free(ins);
}

/*

=item C<int ins_print(imc_info_t * imcc, PIOHANDLE io, const Instruction *ins)>

Print details of instruction C<ins> into file fd.

=cut

*/

#define REGB_SIZE 256
PARROT_IGNORABLE_RESULT
int
ins_print(ARGMOD(imc_info_t * imcc), PIOHANDLE io, ARGIN(const Instruction *ins))
{
    ASSERT_ARGS(ins_print)
    char regb[IMCC_MAX_FIX_REGS][REGB_SIZE];
    /* only long key constants can overflow */
    char *regstr[IMCC_MAX_FIX_REGS];
    int i;
    int len;

    /* comments, labels and such */
    if (!ins->symregs[0] || !strchr(ins->format, '%'))
        return Parrot_io_pprintf(imcc->interp, io, "%s", ins->format);

    for (i = 0; i < ins->symreg_count; i++) {
        const SymReg *p = ins->symregs[i];
        if (!p)
            continue;

        if (p->type & VT_CONSTP)
            p = p->reg;

        if (p->color >= 0 && REG_NEEDS_ALLOC(p)) {
            snprintf(regb[i], REGB_SIZE, "%c%d", p->set, (int)p->color);
            regstr[i] = regb[i];
        }
        else if (p->type & VTREGKEY) {
            const SymReg *k = p;

            *regb[i] = '\0';

            while ((k = k->nextkey) != NULL) {
                const size_t used = strlen(regb[i]);

                if (k->reg && k->reg->color >= 0)
                    snprintf(regb[i]+used, REGB_SIZE - used, "%c%d",
                            k->reg->set, (int)k->reg->color);
                else
                    strncat(regb[i], k->name, REGB_SIZE - used - 1);

                if (k->nextkey)
                    strncat(regb[i], ";", REGB_SIZE - strlen(regb[i]) - 1);
            }

            regstr[i] = regb[i];
        }
        else if (p->type == VTCONST
             &&  p->set  == 'S'
             && *p->name != '"'
             && *p->name != '\'') {
            /* unquoted string const */
            snprintf(regb[i], REGB_SIZE, "\"%s\"", p->name);
            regstr[i] = regb[i];
        }
        else
            regstr[i] = p->name;
    }

    switch (ins->opsize-1) {
      case -1:        /* labels */
      case 1:
        len = Parrot_io_pprintf(imcc->interp, io, ins->format, regstr[0]);
        break;
      case 2:
        len = Parrot_io_pprintf(imcc->interp, io, ins->format, regstr[0], regstr[1]);
        break;
      case 3:
        len = Parrot_io_pprintf(imcc->interp, io, ins->format, regstr[0], regstr[1], regstr[2]);
        break;
      case 4:
        len = Parrot_io_pprintf(imcc->interp, io, ins->format, regstr[0], regstr[1], regstr[2],
                    regstr[3]);
        break;
      case 5:
        len = Parrot_io_pprintf(imcc->interp, io, ins->format, regstr[0], regstr[1], regstr[2],
                    regstr[3], regstr[4]);
        break;
      case 6:
        len = Parrot_io_pprintf(imcc->interp, io, ins->format, regstr[0], regstr[1], regstr[2],
                    regstr[3], regstr[4], regstr[5]);
        break;
      default:
        Parrot_io_eprintf(imcc->interp, "unhandled: opsize (%d), op %s, fmt %s\n",
                ins->opsize, ins->opname, ins->format);
        exit(EXIT_FAILURE);
        break;
    }

    return len;
}

/*

=item C<static int e_pasm_open(imc_info_t * imcc, STRING *path)>

Opens the path to an .pasm file for writing, and stores the os handle in
C<imcc->write_pasm>.

=cut

*/

static int
e_pasm_open(ARGMOD(imc_info_t * imcc), ARGIN(STRING *path))
{
    ASSERT_ARGS(e_pasm_open)
    const Parrot_Interp interp = imcc->interp;
    PMC *handle = PMCNULL;

    if (Parrot_io_open(interp, handle, path, Parrot_str_new_constant(interp, "w")))
        imcc->write_pasm = Parrot_io_get_os_handle(interp, handle);
    if (!imcc->write_pasm)
        Parrot_ex_throw_from_c_args(interp, NULL, EXCEPTION_PIO_ERROR,
                                    "Cannot open output file %Ss", path);

    return 1;
}

/*

=item C<void emit_open(imc_info_t * imcc, STRING *path)>

Opens the output file for writing, either C<e_pbc_open> or C<e_pasm_open>.

=cut

*/

void
emit_open(ARGMOD(imc_info_t * imcc), ARGIN(STRING *path))
{
    ASSERT_ARGS(emit_open)
    imcc->dont_optimize = 0;
    if (UNLIKELY(imcc->write_pasm))
        e_pasm_open(imcc, path);
    else
        e_pbc_open(imcc);
}

/*

=item C<void e_pasm_out(imc_info_t * imcc, const char *fmt, ...)>

Writes a pasm line to global output fh, and prints DEBUG_PBC output to stderr.

=cut

*/

void
e_pasm_out(ARGMOD(imc_info_t * imcc), ARGIN(const char *fmt), ...)
{
    ASSERT_ARGS(e_pasm_out)
    va_list ap;

    if (!(imcc->write_pasm || (DEBUG_PBC & imcc->debug)))
        return;
    va_start(ap, fmt);
    if (imcc->write_pasm)
        Parrot_io_pprintf(imcc->interp, imcc->write_pasm, fmt, ap);
    if (DEBUG_PBC & imcc->debug)
        imcc_vfprintf(imcc, Parrot_io_STDERR(imcc->interp), fmt, ap);
    va_end(ap);
}

/*

=item C<void emit_flush(imc_info_t * imcc, void *param, IMC_Unit *unit)>

Flushes the emitter by emitting all the instructions in the current
IMC_Unit C<unit>.

=cut

*/

void
emit_flush(ARGMOD(imc_info_t * imcc), ARGIN_NULLOK(void *param),
        ARGIN(IMC_Unit *unit))
{
    ASSERT_ARGS(emit_flush)
    Instruction *ins;

    if (0 && UNLIKELY(imcc->write_pasm)) {
        for (ins = unit->instructions; ins; ins = ins->next) {
            IMCC_debug(imcc, DEBUG_IMC, "emit %d\n", ins);

            if ((ins->type & ITLABEL) || ! *ins->opname)
                ins_print(imcc, imcc->write_pasm, ins);
            else {
                Parrot_io_pprintf(imcc->interp, imcc->write_pasm, "\t%s ", ins->opname);
                ins_print(imcc, imcc->write_pasm, ins);
            }
            Parrot_io_pprintf(imcc->interp, imcc->write_pasm, "\n");
        }
    }
    else {
        e_pbc_new_sub(imcc, param, unit);

        for (ins = unit->instructions; ins; ins = ins->next) {
            IMCC_debug(imcc, DEBUG_IMC, "emit %d\n", ins);
            e_pbc_emit(imcc, param, unit, ins);
        }

        e_pbc_end_sub(imcc, param, unit);
    }
}

/*

=item C<void emit_close(imc_info_t *imcc)>

Closes the given emitter.
Fixup globals in pasm or pbc output.

=cut

*/

void
emit_close(ARGMOD(imc_info_t *imcc))
{
    ASSERT_ARGS(emit_close)
    e_pbc_close(imcc);
}

/*

=back

=cut

*/

/*
 * Local variables:
 *   c-file-style: "parrot"
 * End:
 * vim: expandtab shiftwidth=4 cinoptions='\:2=2' :
 */

/****************************************************************************

 check_ast.c:
 Implements functions required for checking the AST types according to scopes.
 Many type rules are checked in this part according to the type_id, lvalue and
 the sym_id in scopes.

*****************************************************************************/

#include "c2java.h"


typedef void (*ast_checker)(ast_node *n);

static ast_checker g_ast_checkers[AST_TYPE_LIMIT];
static const char* g_ast_names[AST_TYPE_LIMIT];

/* report error */

void report_error(const char *fmt, ...)
{
    va_list args;
    char buffer[1024];

    va_start(args, fmt);
    vsnprintf(buffer, sizeof buffer, fmt, args);
    va_end(args);

    fprintf(stdout, "Error.\n");
    fprintf(stderr, "Error: %s\n", buffer);
    exit(1);
}

static int trace_type(int type_id)
{
    if (type_id == 1) return 1;
    struct type_node *t = find_type_node(type_id);
    if (t->ty == FUNC) return t->as_func.ret_type;
    return type_id;
}

static int is_type_of_int(int type_id)
{
    return trace_type(type_id) == 1;
}

/* check in current scope if the sym_id exists */

static struct vscope_node* check_vscope(struct vscope *v, int sym_id)
{
    struct vscope_node *n = v->list;
    for (; n; n = n->next)
    {
        if (n->sym_id == sym_id) return n;
    }
    return NULL;
}

/* check in all scope if the sym_id exists */
static struct vscope_node* check_vscope_all(struct vscope *v, int sym_id)
{
    struct vscope *s = v;
    struct vscope_node *n = s->list;
    for (; s; s = s->parent)
    {
        for (n = s->list; n; n = n->next)
        {
            if (n->sym_id == sym_id) return n;
        }
    }
    return NULL;
}

static struct tscope_node* check_tscope(struct tscope *t, int sym_id)
{
    struct tscope_node *n = t->list;
    for (; n; n = n->next)
    {
        if (n->sym_id == sym_id) return n;
    }
    return NULL;
}

static struct tscope_node* check_tscope_all(struct tscope *t, int sym_id)
{
    struct tscope *s = t;
    struct tscope_node *n = s->list;
    for (; s; s = s->parent)
    {
        for (n = s->list; n; n = n->next)
        {
            if (n->sym_id == sym_id) return n;
        }
    }
    return NULL;
}

int offset = 0;

static void check_list(ast_node *n)
{
    ast_node *p = n;
    for (; p; p = p->list.tail)
    {
        check_ast(p->list.head);
        p->fg = p->list.head->fg;
    }
}

/* Reserved words can not be used as identifiers. Reserved words can be found in TOKENs */

/* record the def_type */
static int def_type;

static void check_def(ast_node *n)
{
    def_type = n->def.def_type;
    if (def_type != 1)
    {
        /* if def -> stspec decs */
        struct type_node *t = find_type_node(n->def.def_type);
        if (check_tscope_all(current_tscope, t->as_struct.sym_name))
        {
            /* if the struct has been declared */
            if (!t->as_struct.fields)
                /* if it is the form "struct T v;" without the fields part, it is a variable declaration */
                def_type = lookup_ttype(t->as_struct.sym_name);
            else
            {
                /* defines a new struct in current scope */
                if (check_tscope(current_tscope, t->as_struct.sym_name))
                {
                    report_error("The struct %s has already been declared", symname(t->as_struct.sym_name));
                }
                else tscope_addnode(current_tscope, t->as_struct.sym_name, t->type_id);
            }
        }
        else tscope_addnode(current_tscope, t->as_struct.sym_name, t->type_id);
    }
    n->type_id = def_type;
    check_ast(n->def.decs);
    n->fg = n->def.decs->fg;
}


static void check_dec(ast_node *n)
{
    check_ast(n->dec.var);
    n->type_id = def_type;
    if (n->dec.init)
    {
        check_ast(n->dec.init);
        ast_node *p = n->dec.init;
        if (p->type == AST_LIST)
        {
            // Check the initialized element have the same type with the left variable.
            if (find_type_node(n->dec.var->type_id)->ty != ARRAY)
                report_error("Initialized type error, assign array elements to a non-array type");
            // Check the size of the array initialization should equal or less than the size of the left array.
            int size = find_type_node(n->dec.var->type_id)->as_array.size;
            int i = 0;
            for (; p; p = p->list.tail, ++i)
            {
                if (!is_type_of_int(p->list.head->type_id))
                    report_error("the init type is not INT");
            }
            if (i > size) report_error("The number in the initialization surmout the declared size of the array");
        }
        else if (trace_type(n->type_id) != trace_type(n->dec.init->type_id))
        {
            report_error("init type are not the same with def type");
        }
    }
    n->offset = offset;
    n->fg = n->dec.var->fg;
}

/* construct array type */

/* row major */
static void check_subvar(ast_node *n)
{
    int old_def_type = def_type;

    def_type = array_type(n, def_type);
    check_ast(n->subvar.var);
    n->type_id = n->subvar.var->type_id; /* propagate real type up for declaration */
    n->offset = n->subvar.var->offset;   /* propagate offset for array initialization */

    def_type = old_def_type;
    n->fg = n->subvar.var->fg;
}

static void check_var(ast_node *n)
{
    /* check variables should not be re-declared */
    if (check_vscope(current_vscope, n->var.sym_name))
        report_error("%s has already been declared", symname(n->var.sym_name));

    n->type_id = def_type;
    n->offset = offset - 4;
    int m = get_memspace(n->type_id);
    offset -= m;
    vscope_addnode(current_vscope, n->var.sym_name, def_type, n->offset);
    if (current_vscope->parent == NULL) n->fg = 'g'; /* global use $gp */
}

static int length_of(ast_node *n)
{
    if (n && n->type == AST_LIST)
    {
        return 1 + length_of(n->list.tail);
    }
    return 0;
}

static void check_funcdef(ast_node *n)
{
    /* check the return type of function */
    if (!is_type_of_int(n->funcdef.ret_type))
        report_error("function return type is not INT");

    n->type_id = 1; /* return type is INT */

    int old_offset = offset;
    offset = length_of(n->funcdef.funchead->funchead.paras) * 4 + 8; /* 8 for $fp and $ra */
    check_ast(n->funcdef.funchead);
    offset = 0;
    check_ast(n->funcdef.funcbody);
    n->framesize = -offset;
    offset = old_offset;
    end_tscope();
    end_vscope();
}

static void check_funchead(ast_node *n)
{
    /* check function name should not be re-declared */
    if (check_vscope(current_vscope, n->funchead.sym_name))
        report_error("%s has already been declared", symname(n->funchead.sym_name));

    n->type_id = func_type(n);
    vscope_addnode(current_vscope, n->funchead.sym_name, n->type_id, -1);
    begin_vscope();
    begin_tscope();
    check_ast(n->funchead.paras);
}

/* check the parameters declared in a function */
static void check_para(ast_node *n)
{
    /* check the parameter's type should be INT */
    if (!is_type_of_int(n->para.para_type))
        report_error("parameter %s's type is not INT", symname(n->para.var->var.sym_name));
    /* check parametes should not have the same name */
    if (check_vscope(current_vscope, n->para.var->var.sym_name))
        report_error("%s has been declared twice", symname(n->para.var->var.sym_name));
    n->type_id = n->para.para_type;
    def_type = n->type_id;
    check_ast(n->para.var);
}

/* check if the structure type has been declared */
static void check_stdef(ast_node *n)
{
    if (check_tscope(current_tscope, n->stdef.sym_name))
        report_error("the structure type has already be declared: %s", symname(n->stdef.sym_name));
    n->type_id = struct_type(n);
    tscope_addnode(current_tscope, n->stdef.sym_name, n->type_id);
    begin_vscope();
    begin_tscope();
    check_ast(n->stdef.defs);
    end_tscope();
    end_vscope();
}


static void check_compound_stmt(ast_node *n)
{
    check_ast(n->compound_stmt.defs);
    check_ast(n->compound_stmt.stmts);
}

static void check_expr_stmt(ast_node *n)
{
    check_ast(n->expr_stmt.expr);
    n->type_id = n->expr_stmt.expr->type_id;
    n->lvalue = n->expr_stmt.expr->lvalue;
}

/* The condition of if statement should be an expression with int type */

static void check_if_stmt(ast_node *n)
{
    check_ast(n->if_stmt.cond);
    if (n->if_stmt.cond->type_id != 1) report_error("The condition of if statement should be an expression with int type");
    check_ast(n->if_stmt.then);
    check_ast(n->if_stmt.els);
}


/* check wheather break and continue are in for loop */
static int loop_level = 0;

static void check_for_stmt(ast_node *n)
{
    check_ast(n->for_stmt.init);
    if (n->for_stmt.cond)
    {
        check_ast(n->for_stmt.cond);
        /* The condition of for should be an expression with int type or epsilon */
        if (n->for_stmt.cond->type_id != 1)
            report_error("The condition of for should be an expression with int type or epsilon");
    }
    check_ast(n->for_stmt.incr);
    loop_level++;
    check_ast(n->for_stmt.body);
    loop_level--;
}

static void check_continue_stmt(ast_node *n)
{
    if (loop_level > 0)
    {
        /* in loop, ok */
    }
    else report_error("continue not in loop");
}

static void check_break_stmt(ast_node *n)
{
    if (loop_level > 0)
    {
        /* in loop, ok */
    }
    else report_error("break not in loop");
}



static void check_return_stmt(ast_node *n)
{
    check_ast(n->return_stmt.retval);
    n->type_id = n->return_stmt.retval->type_id;
    /* check the return type should be INT */
    if (!is_type_of_int(n->type_id))
        report_error("return type is not INT");
}

/* Only expression with type int can be involved in arithmetic */

static void check_binop(ast_node *n)
{
    check_ast(n->binop.lhs);
    if (!is_type_of_int(n->binop.lhs->type_id))
        report_error("left hand side is not INT");

    check_ast(n->binop.rhs);
    if (!is_type_of_int(n->binop.rhs->type_id))
        report_error("right hand side is not INT");

    n->type_id = 1;
    /* Right-value can not be assigned by any value or expression */
    /* XXX_ASSIGN operators are also considered */
    if (binop_reuse(n->binop.op)) {
        if (!n->binop.lhs->lvalue)
            report_error("Right-value can not be assigned by any value or expression");
        n->lvalue = 1;
    }
    else n->lvalue = 0;
}

static void check_prefix(ast_node *n)
{
    check_ast(n->prefix.unary);
    n->type_id = n->prefix.unary->type_id;
    n->lvalue = 0;
}

static void check_postfix(ast_node *n)
{
    check_ast(n->postfix.unary);
    n->type_id = n->postfix.unary->type_id;
    n->lvalue = 0;
    n->fg = n->postfix.unary->fg;
}

/* Use [] operator to a non-array variable is not allowed */
static void check_indexing(ast_node *n)
{
    check_ast(n->indexing.postfix);
    struct type_node *t = find_type_node(n->indexing.postfix->type_id);
    if (t->ty != ARRAY)
    {
        report_error("[] can only follow an array type");
    }
    n->type_id = t->as_array.elem_type_id;

    #ifdef DEBUG
        fprintf(stderr, "[DEBUG] check_indexing n->indexing.postfix->type_id=%d\n", n->indexing.postfix->type_id);
        fprintf(stderr, "[DEBUG] check_indexing n->type_id=%d\n", n->type_id);
    #endif

    check_ast(n->indexing.expr);
    n->lvalue = 1;
    n->fg = n->indexing.postfix->fg;
}

/* The number and type of variable(s) passed should match the definition of the function */

static void check_func_call(ast_node *n)
{
    if (!check_vscope_all(current_vscope, n->func_call.postfix->id.sym_name))
    {
        report_error("function %s not declared", symname(n->func_call.postfix->id.sym_name));
    }

    check_ast(n->func_call.postfix);
    n->type_id = n->func_call.postfix->type_id;
    n->lvalue = 0;
    n->func_call.postfix->lvalue = 0;
    struct type_node *t = find_type_node(n->type_id);

    /* check the identifier of a function call is of type function */
    if (t->ty != FUNC)
    {
        report_error("the identifier %s is not of type FUNC", symname(n->func_call.postfix->id.sym_name));
    }

    /* check the argument number and type */
    ast_node *p = n->func_call.args;
    for (int i = 0; i < t->as_func.para_num; ++i)
    {
        if (!p)
        {
            report_error("insufficient arguments in function %s", symname(n->func_call.postfix->id.sym_name));
        }
        check_ast(p->list.head);
        int given = trace_type(p->list.head->type_id);
        int expected = t->as_func.para_type[i];
        if (given != expected)
        {
            report_error("argument %d (%d) in function %s is not the type declared (%d)",
                        i, given,
                        symname(n->func_call.postfix->id.sym_name), expected);
        }
        p = p->list.tail;
    }
    if (p)
    {
        report_error("too many arguments in function %s", symname(n->func_call.postfix->id.sym_name));
    }
    n->fg = n->func_call.postfix->fg;
}

/* . operator can only be used to a struct variable */
static void check_member(ast_node *n)
{
    check_ast(n->member.postfix);

    struct type_node *t = find_type_node(n->member.postfix->type_id);
    if (t->ty != STRUCTURE)
        report_error(". can only follow a structure type");

    int mem_exist = 0;
    for (struct field_list *f = t->as_struct.fields; f; f = f->next)
    {
        if (f->sym_id == n->member.sym_name)
        {
            mem_exist = 1;
            break;
        }
    }
    if (!mem_exist)
        report_error("%s is not declared in the structure", symname(n->id.sym_name));

    n->type_id = 1; /* INT */
    n->lvalue = 1;
    n->fg = n->member.postfix->fg;
}

static void check_id(ast_node *n)
{
    /* check variables have been declared before usage */

    if (!check_vscope_all(current_vscope, n->id.sym_name))
    {
        report_error("%s is not declared", symname(n->id.sym_name));
    }

    struct vscope_node *vnode = find_vnode(n->id.sym_name);
    n->type_id = vnode->type_id;
    n->offset = vnode->offset;
    n->lvalue = 1;

    if (vnode->scope->parent == NULL) n->fg = 'g'; /* global use $gp */
}

static void check_const(ast_node *n)
{
    n->type_id = 1;
    n->lvalue = 0;
}

/*
static void check_init_arg(ast_node *n)
{
    if (!check_vscope_all(current_vscope, n->init_arg.sym_name))
            report_error("%d isn't declared", n->init_arg.sym_name);

    else {}
    check_ast(n->init_arg.expr);
}
*/

static void init_ast_checkers()
{
    if (g_ast_checkers[0]) return;
    g_ast_checkers[AST_LIST] = check_list;
    g_ast_checkers[AST_FUNCDEF] = check_funcdef;
    g_ast_checkers[AST_FUNCHEAD] = check_funchead;
    g_ast_checkers[AST_PARA] = check_para;
    g_ast_checkers[AST_STDEF] = check_stdef;
    g_ast_checkers[AST_VAR] = check_var;
    g_ast_checkers[AST_SUBVAR] = check_subvar;
    g_ast_checkers[AST_COMPOUND_STMT] = check_compound_stmt;
    g_ast_checkers[AST_EXPR_STMT] = check_expr_stmt;
    g_ast_checkers[AST_IF_STMT] = check_if_stmt;
    g_ast_checkers[AST_FOR_STMT] = check_for_stmt;
    g_ast_checkers[AST_RETURN_STMT] = check_return_stmt;
    g_ast_checkers[AST_CONTINUE_STMT] = check_continue_stmt;
    g_ast_checkers[AST_BREAK_STMT] = check_break_stmt;
    g_ast_checkers[AST_DEF] = check_def;
    g_ast_checkers[AST_DEC] = check_dec;
    g_ast_checkers[AST_BINOP] = check_binop;
    g_ast_checkers[AST_PREFIX] = check_prefix;
    g_ast_checkers[AST_POSTFIX] = check_postfix;
    g_ast_checkers[AST_INDEXING] = check_indexing;
    g_ast_checkers[AST_FUNC_CALL] = check_func_call;
    g_ast_checkers[AST_MEMBER] = check_member;
    g_ast_checkers[AST_ID] = check_id;
    g_ast_checkers[AST_CONST] = check_const;
    // g_ast_checkers[AST_INIT_ARG] = check_init_arg;
    g_ast_names[AST_LIST] = "check_list";
    g_ast_names[AST_FUNCDEF] = "check_funcdef";
    g_ast_names[AST_FUNCHEAD] = "check_funchead";
    g_ast_names[AST_PARA] = "check_para";
    g_ast_names[AST_STDEF] = "check_stdef";
    g_ast_names[AST_VAR] = "check_var";
    g_ast_names[AST_SUBVAR] = "check_subvar";
    g_ast_names[AST_COMPOUND_STMT] = "check_compound_stmt";
    g_ast_names[AST_EXPR_STMT] = "check_expr_stmt";
    g_ast_names[AST_IF_STMT] = "check_if_stmt";
    g_ast_names[AST_FOR_STMT] = "check_for_stmt";
    g_ast_names[AST_RETURN_STMT] = "check_return_stmt";
    g_ast_names[AST_CONTINUE_STMT] = "check_continue_stmt";
    g_ast_names[AST_BREAK_STMT] = "check_break_stmt";
    g_ast_names[AST_DEF] = "check_def";
    g_ast_names[AST_DEC] = "check_dec";
    g_ast_names[AST_BINOP] = "check_binop";
    g_ast_names[AST_PREFIX] = "check_prefix";
    g_ast_names[AST_POSTFIX] = "check_postfix";
    g_ast_names[AST_INDEXING] = "check_indexing";
    g_ast_names[AST_FUNC_CALL] = "check_func_call";
    g_ast_names[AST_MEMBER] = "check_member";
    g_ast_names[AST_ID] = "check_id";
    g_ast_names[AST_CONST] = "check_const";
}

void check_ast(ast_node *n)
{
    init_ast_checkers();
    if (n)
    {
        #ifdef DEBUG
            fprintf(stderr, "[DEBUG] check_ast n->type=%s\n", g_ast_names[n->type]);
        #endif

        g_ast_checkers[n->type](n);
    }
}

extern int main_sym;

void check_semantics(ast_node *ast)
{
    begin_vscope();
    begin_tscope();

    vscope_addnode(current_vscope, sym("read"), builtin_func_type(0), -1);
    vscope_addnode(current_vscope, sym("write"), builtin_func_type(1), -1);

    check_ast(ast);

    /* check program must contain a function int main() to be the entrance */
    if (!check_vscope(current_vscope, sym("main")))
        report_error("the program doesn't contain int main() function as entrance");

    main_sym = sym("main");

    end_tscope();
    end_vscope();
}


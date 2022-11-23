%{

#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <cstdio>
#include "types.h"
#include "vm.h"

extern FILE * yyin;
int yylex();
void yyerror(const char *);

struct bc_segment
{
    std::vector<uint8_t> bc;
    size_t ref_segment;
    size_t ref_bc;
};

std::vector<bc_segment> segments(1);
size_t current_segment;
std::vector<std::string> string_pool;
std::unordered_map<std::string, size_t> string_idx;

#define panic(msg) { puts(msg); YYABORT; }
#define E(f) try { f; } catch (const char * e) { puts(e); YYABORT; }

void C(uint8_t bc)
{
    segments[current_segment].bc.push_back(bc);
}

size_t get_pos()
{
    return segments[current_segment].bc.size();
}

void fill_pos(size_t pos, uint8_t bc)
{
    segments[current_segment].bc[pos] = bc;
}

uint8_t get_str_idx(const char * s)
{
    size_t idx;
    std::string str(s);
    auto p = string_idx.find(str);
    if (p == string_idx.end())
    {
        idx = string_pool.size();
        string_pool.push_back(str);
        string_idx[str] = idx;
    }
    else
    {
        idx = p->second;
    }
    if (idx >= 256) throw "ERROR: String pool too large (>= 256)";
    return (uint8_t)idx;
}

void parse_lv_read(const lval & lv)
{
    switch (lv.t)
    {
    case lval::VAR:
        C(LOAD);
        C(get_str_idx(lv.s));
        break;
    case lval::SUPER:
        C(LOAD_SUPER);
        C(get_str_idx(lv.s));
        break;
    case lval::FIELD:
        C(LOAD_FIELD);
        C(get_str_idx(lv.s));
        break;
    case lval::ITEM:
        C(LOAD_ITEM);
        break;
    }
    delete[] lv.s;
}

void parse_lv_write(const lval & lv)
{
    switch (lv.t)
    {
    case lval::VAR:
        C(STORE);
        C(get_str_idx(lv.s));
        break;
    case lval::SUPER:
        C(STORE_SUPER);
        C(get_str_idx(lv.s));
        break;
    case lval::FIELD:
        C(STORE_FIELD);
        C(get_str_idx(lv.s));
        break;
    case lval::ITEM:
        C(STORE_ITEM);
        break;
    }
    delete[] lv.s;
}

void parse_param(uint8_t level, const char * s)
{
    C(PUSH_ARG); C(level);
    C(STORE); C(get_str_idx(s));
    delete[] s;
}

void parse_push_int(int64_t i)
{
    if (i >= INT8_MIN && i <= INT8_MAX)
    {
        C(PUSH_BINT);
        C((int8_t)i);
    }
    else if (i >= INT16_MIN && i <= INT16_MAX)
    {
        C(PUSH_WINT);
        uint16_t ii = (int16_t)i;
        for (int n = 0; n < 2; n++)
            C((uint8_t)(ii >> (n * 8)));
    }
    else if (i >= INT32_MIN && i <= INT32_MAX)
    {
        C(PUSH_DWINT);
        uint32_t ii = (int32_t)i;
        for (int n = 0; n < 4; n++)
            C((uint8_t)(ii >> (n * 8)));
    }
    else
    {
        C(PUSH_INT);
        uint64_t ii = i;
        for (int n = 0; n < 8; n++)
            C((uint8_t)(ii >> (n * 8)));
    }
}

void parse_push_float(double f)
{
    C(PUSH_FLOAT);
    uint64_t ii = *(uint64_t *)&f;
    for (int n = 0; n < 8; n++)
        C((uint8_t)(ii >> (n * 8)));
}

void parse_jump_target(size_t from)
{
    size_t offset = get_pos() - from;
    if (offset >= 128) throw "ERROR: Jumping too far (offset >= 128)";
    fill_pos(from - 1, (uint8_t)offset);
}

void begin_closure()
{
    C(PUSH_CLOSURE);
    size_t ref_bc = get_pos();
    C(0);
    size_t idx = segments.size();
    segments.emplace_back();
    segments[idx].ref_segment = current_segment;
    segments[idx].ref_bc = ref_bc;
    current_segment = idx;
}

void end_closure()
{
    current_segment = segments[current_segment].ref_segment;
}

std::vector<uint8_t> get_script()
{
    size_t idx = segments[0].bc.size();
    for (size_t i = 1; i < segments.size(); i++)
    {
        if (idx >= 256) throw "ERROR: Script too long (>= 256)";
        const bc_segment & seg = segments[i];
        segments[seg.ref_segment].bc[seg.ref_bc] = (uint8_t)idx;
        idx += seg.bc.size();
    }
    std::vector<uint8_t> script;
    for (const bc_segment & seg : segments)
        script.insert(script.end(), seg.bc.begin(), seg.bc.end());
    return script;
}

%}

%union
{
    long long i;
    double f;
    char * s;
    lval lv;
    super sp;
    size_t pos;
}

%token <i> INT_CONST
%token <f> FLOAT_CONST
%token <s> STRING_CONST NAME

%type <i> param_list
%type <pos> cond_return_dummy
%type <pos> loop_dummy
%type <pos> op_or_dummy
%type <pos> op_and_dummy
%type <pos> op_cond_body
%type <pos> op_cond_dummy
%type <lv> lv
%type <sp> super_name
%type <i> exp_list

%right '?' ':'
%left OP_OR
%left OP_AND
%left '|'
%left '^'
%left '&'
%left OP_EQ OP_NE
%left '>' '<' OP_GE OP_LE
%left OP_SHL OP_SHR OP_USHR
%left '+' '-'
%left '*' '/' '%'
%left OP_POS OP_NEG '!' '~' '#'

%%

st_list :               { C(PUSH_SELF); C(RETURN); }
        | st st_list

st      : ';'
        | lv '=' exp ';'    { E(parse_lv_write($1)); }
        | '>' param_list ';'
        | '<' exp ';'       { C(RETURN); }
        | '<' '?' exp ',' cond_return_dummy exp ';' { C(RETURN); E(parse_jump_target($5)); }
        | ':' loop_dummy exp ';'                    {
                                                        C(JUMP_IF);
                                                        size_t offset = get_pos() + 1 - $2;
                                                        if (offset > 128) throw "ERROR: Jumping too far (offset < -128)";
                                                        C(-(int8_t)offset);
                                                    }
        | OP_SHR lv ';'     { C(IN); E(parse_lv_write($2)); }
        | OP_SHL exp ';'    { C(OUT); }
        | exp ';'           { C(POP); }

cond_return_dummy   : { C(JUMP_UNLESS); C(0); $$ = get_pos(); }

loop_dummy          : { $$ = get_pos(); }

param_list  : NAME                  { $$ = 0; E(parse_param(0, $1)); }
            | param_list ',' NAME   {
                                        $$ = $1 + 1;
                                        if ($$ >= 256) panic("ERROR: Too many parameters (>= 256)");
                                        E(parse_param((uint8_t)$$, $3));
                                    }

exp     : INT_CONST             { parse_push_int($1); }
        | FLOAT_CONST           { parse_push_float($1); }
        | STRING_CONST          { C(PUSH_STRING); E(C(get_str_idx($1))); delete[] $1; }
        | lv                    { E(parse_lv_read($1)); }
        | '+' exp %prec OP_POS  { C(POS); }
        | '-' exp %prec OP_NEG  { C(NEG); }
        | '!' exp               { C(NOT); }
        | '~' exp               { C(BINV); }
        | '#' exp               { C(LEN); }
        | exp '+' exp           { C(ADD); }
        | exp '-' exp           { C(SUB); }
        | exp '*' exp           { C(MUL); }
        | exp '/' exp           { C(DIV); }
        | exp '%' exp           { C(REM); }
        | exp OP_EQ exp         { C(CMP_EQ); }
        | exp OP_NE exp         { C(CMP_NE); }
        | exp '>' exp           { C(CMP_GT); }
        | exp '<' exp           { C(CMP_LT); }
        | exp OP_GE exp         { C(CMP_GE); }
        | exp OP_LE exp         { C(CMP_LE); }
        | exp '|' exp           { C(BOR); }
        | exp '^' exp           { C(BXOR); }
        | exp '&' exp           { C(BAND); }
        | exp OP_SHL exp        { C(SHL); }
        | exp OP_SHR exp        { C(SHR); }
        | exp OP_USHR exp       { C(USHR); }
        | exp OP_OR op_or_dummy exp     { E(parse_jump_target($3)); }
        | exp OP_AND op_and_dummy exp   { E(parse_jump_target($3)); }
        | exp '?' op_cond_body ':' exp  { E(parse_jump_target($3)); }
        | '(' exp ')'
        | exp '(' exp_list ')'              { C(CALL); C((uint8_t)$3); }
        | '{' closure_begin st_list '}'     { end_closure(); C(CALL); C(0); }
        | '@' '{' closure_begin st_list '}' { end_closure(); }
        | '[' exp_list ']'                  { C(NEW_ARRAY); C((uint8_t)$2); }
        | '@' NAME                          { C(LOAD_LIB); E(C(get_str_idx($2))); delete[] $2; }

op_or_dummy     :   { C(DUP); C(JUMP_IF); C(0); $$ = get_pos(); C(POP); }

op_and_dummy    :   { C(DUP); C(JUMP_UNLESS); C(0); $$ = get_pos(); C(POP); }

op_cond_body    : op_cond_dummy exp {
                                        C(JUMP); C(0); $$ = get_pos();
                                        E(parse_jump_target($1));
                                    }

op_cond_dummy   :   { C(JUMP_UNLESS); C(0); $$ = get_pos(); }

lv      : NAME              { $$ = {lval::VAR, $1}; }
        | super_name        {
                                uint8_t level = (uint8_t)$1.level;
                                if (level)
                                {
                                    C(PUSH_SUPER);
                                    C((uint8_t)$1.level);
                                    $$ = {lval::FIELD, $1.s};
                                }
                                else $$ = {lval::SUPER, $1.s};
                            }
        | exp '.' NAME      { $$ = {lval::FIELD, $3}; }
        | exp '[' exp ']'   { $$ = {lval::ITEM, nullptr}; }

super_name  : '$' NAME          { $$ = {0, $2}; }
            | '$' super_name    { $$ = $2; if (++$$.level >= 256) panic("ERROR: Too high level of super (>= 256)"); }

exp_list:                   { $$ = 0; }
        | exp               { $$ = 1; }
        | exp ',' exp_list  { $$ = $3 + 1; if ($$ >= 256) panic("ERROR: Too many arguments (>= 256)"); }

closure_begin   :   { begin_closure(); }
%%

int main(int argc, char ** argv)
{
    if (argc != 2)
    {
        printf("Usage: %s filename\n", argv[0]);
        return 1;
    }
    if (!(yyin = fopen(argv[1], "r")))
    {
        printf("Failed to read from script file %s\n", argv[1]);
        return 1;
    }
    int rt = yyparse();
    if (rt) return rt;
    fclose(yyin);
    script script;
    script.string_pool = string_pool;
    try { script.code = get_script(); } catch (const char * e) { puts(e); return 1; }
    /* dump_code(script); */
    run_script(script);
    return 0;
}

void yyerror(const char * s)
{
    printf("ERROR: %s\n", s);
}

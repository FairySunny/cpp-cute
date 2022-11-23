#include <unordered_set>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "vm.h"
#include "misc.h"

static char gc_current_status;
static std::unordered_set<gc_base_obj *> gc_obj_list;

template<typename T, typename ... Args>
static gc_obj<T> * new_obj(Args ... args)
{
    gc_obj<T> * obj = new gc_obj<T>(args ...);
    obj->gc_status = gc_current_status;
    gc_obj_list.insert(obj);
    return obj;
};

static void delete_obj(gc_base_obj * obj)
{
    if (gc_obj_list.erase(obj))
        delete obj;
}

struct stack_info
{
    closure_info * c_info;
    const script * s;
    int param_count;
    int stack_return;
    int pc_return;
};

class vm_error
{
    std::string msg;
public:
    template<typename ... Args>
    vm_error(const char * fmt, Args ... args)
    {
        size_t len = strlen(fmt);
        char * buf = new char[len + 20];
        snprintf(buf, len + 20, fmt, args ...);
        msg = buf;
        delete[] buf;
    }
    void print() const
    {
        printf("ERROR: %s\n", msg.c_str());
    }
};

static void mark_closure_info(closure_info * ci);

static void mark(const type_and_value & tv)
{
    switch (tv.t)
    {
    case STRING:
        if (tv.v.o->gc_status != gc_current_status)
            tv.v.s->gc_status = gc_current_status;
        break;
    case OBJECT:
        if (tv.v.o->gc_status != gc_current_status)
        {
            tv.v.o->gc_status = gc_current_status;
            for (auto & p : tv.v.o->value)
                mark(p.second);
        }
        break;
    case ARRAY:
        if (tv.v.o->gc_status != gc_current_status)
        {
            tv.v.a->gc_status = gc_current_status;
            for (auto & tv : tv.v.a->value)
                mark(tv);
        }
        break;
    case CLOSURE:
        if (tv.v.o->gc_status != gc_current_status)
        {
            tv.v.c->gc_status = gc_current_status;
            mark_closure_info(tv.v.c->value.super);
        }
        break;
    }
}

static void mark_closure_info(closure_info * ci)
{
    if (ci && ci->gc_status != gc_current_status)
    {
        ci->gc_status = gc_current_status;
        mark(ci->value.self);
        mark_closure_info(ci->value.super);
    }
}

static void gc(std::vector<type_and_value> stack, std::vector<stack_info> info)
{
    gc_current_status = 1 - gc_current_status;
    for (const type_and_value & tv : stack)
        mark(tv);
    for (const stack_info & si : info)
        mark_closure_info(si.c_info);
    auto list = gc_obj_list;
    for (auto p : list)
        if (p->gc_status != gc_current_status)
            delete_obj(p);
}

static void cleanup()
{
    for (auto p : gc_obj_list)
        delete p;
}

static const char * type_name(int type)
{
    const char * const names[] = {"nil", "int", "float", "bool", "string", "object", "array", "closure"};
    if (type < 0 || type >= 8)
        return "[unknown type]";
    return names[type];
}

static uint8_t code_next(const std::vector<uint8_t> * code, int & pc)
{
    if (pc < 0 || pc >= code->size())
        throw vm_error("PC (=%d) goes out of script range", pc);
    return (*code)[pc++];
}

static const std::string & get_string(const std::vector<std::string> * string_pool, uint8_t idx)
{
    if (idx >= string_pool->size())
        throw vm_error("String pool index (%d) out of range", idx);
    return (*string_pool)[idx];
}

static type_and_value stack_pop(std::vector<type_and_value> & stack, int ptr)
{
    if (stack.size() <= ptr)
        throw vm_error("Current stack frame empty");
    type_and_value rt = stack.back();
    stack.pop_back();
    return rt;
}

static type_and_value & stack_top(std::vector<type_and_value> & stack, int ptr, int offset = 0)
{
    size_t sz = stack.size() - offset;
    if (sz <= ptr)
        throw vm_error("Current stack frame empty");
    return stack.at(sz - 1);
}

static void check_type(const type_and_value & tv, type t)
{
    if (tv.t == t) return;
    throw vm_error("Invalid type %s, %s expected", type_name(tv.t), type_name(t));
}

static void check_types(const type_and_value & tv, int types)
{
    if (types & (1 << tv.t)) return;
    throw vm_error("Invalid type %s", type_name(tv.t));
}

static vm_error op_type_error(const char * op, type t1, type t2)
{
    return vm_error("Cannot apply '%s' on types %s and %s", op, type_name(t1), type_name(t2));
}

static bool is_equal(const type_and_value & tv1, const type_and_value & tv2)
{
    if (tv1.t != tv2.t) return false;
    switch (tv1.t)
    {
    case NIL: return true;
    case INT: return tv1.v.i == tv2.v.i;
    case FLOAT: return tv1.v.f == tv2.v.f;
    case BOOL: return tv1.v.b == tv2.v.b;
    case STRING: return tv1.v.s->value == tv2.v.s->value;
    case OBJECT: return tv1.v.o == tv2.v.o;
    case ARRAY: return tv1.v.a == tv2.v.a;
    case CLOSURE: return tv1.v.c == tv2.v.c;
    default: throw vm_error("Unknown type %d", tv1.t);
    }
}

static bool is_greater(const type_and_value & tv1, const type_and_value & tv2)
{
    if (tv1.t != tv2.t)
        throw op_type_error(">", tv1.t, tv2.t);
    switch (tv1.t)
    {
    case INT: return tv1.v.i > tv2.v.i;
    case FLOAT: return tv1.v.f > tv2.v.f;
    case STRING: return tv1.v.s->value > tv2.v.s->value;
    default: throw op_type_error(">", tv1.t, tv2.t);
    }
}

static bool is_less(const type_and_value & tv1, const type_and_value & tv2)
{
    if (tv1.t != tv2.t)
        throw op_type_error("<", tv1.t, tv2.t);
    switch (tv1.t)
    {
    case INT: return tv1.v.i < tv2.v.i;
    case FLOAT: return tv1.v.f < tv2.v.f;
    case STRING: return tv1.v.s->value < tv2.v.s->value;
    default: throw op_type_error("<", tv1.t, tv2.t);
    }
}

static type_and_value value_to_string(const type_and_value & tv)
{
    const int buf_len = 30;
    static char buf[buf_len];
    switch (tv.t)
    {
    case NIL: return new_string("null");
    case INT: snprintf(buf, buf_len, "%lld", tv.v.i); break;
    case FLOAT: snprintf(buf, buf_len, "%f", tv.v.f); break;
    case BOOL: return tv.v.b ? new_string("true") : new_string("false");
    case STRING: return tv;
    case OBJECT: snprintf(buf, buf_len, "object@%p", tv.v.o); break;
    case ARRAY: snprintf(buf, buf_len, "array@%p", tv.v.a); break;
    case CLOSURE: snprintf(buf, buf_len, "closure@%p", tv.v.c); break;
    default: throw vm_error("Unknown type %d", tv.t);
    }
    return new_string(buf);
}

// static void debug_print_value(const type_and_value & tv, int indent = 0)
// {
//     switch (tv.t)
//     {
//     case INT: printf("%lld", tv.v.i); break;
//     case FLOAT: printf("%f", tv.v.f); break;
//     case BOOL: printf(tv.v.b ? "true" : "false"); break;
//     case STRING: printf("\"%s\"", tv.v.s->value.c_str()); break;
//     case OBJECT:
//         printf("{\n");
//         for (auto & p : tv.v.o->value)
//         {
//             for (int i = 0; i < indent + 2; i++) putchar(' ');
//             printf("\"%s\": ", p.first.c_str());
//             debug_print_value(p.second, indent + 2);
//             printf(",\n");
//         }
//         for (int i = 0; i < indent; i++) putchar(' ');
//         putchar('}');
//         break;
//     default: printf("null"); break;
//     }
// }

// static void debug_print_stack(const std::vector<type_and_value> & stack)
// {
//     puts("stack:");
//     for (auto & tv : stack)
//     {
//         printf("- ");
//         debug_print_value(tv);
//         putchar('\n');
//     }
// }

type_and_value new_string(const std::string & str)
{
    return type_and_value{STRING, {.s = new_obj<str_def>(str)}};
}

type_and_value new_empty_object()
{
    return type_and_value{OBJECT, {.o = new_obj<obj_def>()}};
}

type_and_value new_array(arr_def::const_iterator begin, arr_def::const_iterator end)
{
    return type_and_value{ARRAY, {.a = new_obj<arr_def>(begin, end)}};
}

type_and_value new_closure(closure_info * super, const script * s, int addr)
{
    closure * c = new_obj<closure_def>();
    c->value.super = super;
    c->value.s = s;
    c->value.addr = addr;
    return type_and_value{CLOSURE, {.c = c}};
}

closure_info * new_closure_info(closure_info * super, const type_and_value & self)
{
    closure_info * ci = new_obj<closure_info_def>();
    ci->value.super = super;
    ci->value.self = self;
    return ci;
}

void run_script(const script & s)
{
    std::vector<type_and_value> stack = {new_empty_object()};
    obj_def & libs = stack[0].v.o->value;
    load_misc(libs);
    std::vector<stack_info> info;
    info.push_back({new_closure_info(nullptr, new_empty_object()), &s, 0, -1, -1});
    stack_info * cur_info = &info.back();
    obj_def * cur_obj = &cur_info->c_info->value.self.v.o->value;
    auto * code = &s.code;
    auto * string_pool = &s.string_pool;
    int pc = 0;
    int ptr = 1;
    try
    {
        while (1)
        {
            switch (code_next(code, pc))
            {
            case LOAD:
                {
                    uint8_t str_idx = code_next(code, pc);
                    auto p = cur_obj->find(get_string(string_pool, str_idx));
                    if (p == cur_obj->end()) stack.push_back({NIL});
                    else stack.push_back(p->second);
                }
                break;
            case STORE:
                {
                    uint8_t str_idx = code_next(code, pc);
                    type_and_value tv = stack_pop(stack, ptr);
                    if (tv.t == NIL)
                        cur_obj->erase(get_string(string_pool, str_idx));
                    else
                        (*cur_obj)[get_string(string_pool, str_idx)] = tv;
                }
                break;
            case LOAD_SUPER:
                {
                    uint8_t str_idx = code_next(code, pc);
                    closure_info * c_info = cur_info->c_info->value.super;
                    if (!c_info)
                        throw vm_error("Trying to get level 0 super closure which does not exist");
                    type_and_value stv = c_info->value.self;
                    check_type(stv, OBJECT);
                    obj_def * super_obj = &stv.v.o->value;
                    auto p = super_obj->find(get_string(string_pool, str_idx));
                    if (p == super_obj->end()) stack.push_back({NIL});
                    else stack.push_back(p->second);
                }
                break;
            case STORE_SUPER:
                {
                    uint8_t str_idx = code_next(code, pc);
                    closure_info * c_info = cur_info->c_info->value.super;
                    if (!c_info)
                        throw vm_error("Trying to get level 0 super closure which does not exist");
                    type_and_value stv = c_info->value.self;
                    check_type(stv, OBJECT);
                    obj_def * super_obj = &stv.v.o->value;
                    type_and_value tv = stack_pop(stack, ptr);
                    if (tv.t == NIL)
                        super_obj->erase(get_string(string_pool, str_idx));
                    else
                        (*super_obj)[get_string(string_pool, str_idx)] = tv;
                }
                break;
            case LOAD_FIELD:
                {
                    uint8_t str_idx = code_next(code, pc);
                    type_and_value otv = stack_pop(stack, ptr);
                    check_type(otv, OBJECT);
                    obj_def & obj = otv.v.o->value;
                    auto p = obj.find(get_string(string_pool, str_idx));
                    if (p == obj.end()) stack.push_back({NIL});
                    else stack.push_back(p->second);
                }
                break;
            case STORE_FIELD:
                {
                    uint8_t str_idx = code_next(code, pc);
                    type_and_value tv = stack_pop(stack, ptr);
                    type_and_value otv = stack_pop(stack, ptr);
                    check_type(otv, OBJECT);
                    obj_def & obj = otv.v.o->value;
                    if (tv.t == NIL)
                        obj.erase(get_string(string_pool, str_idx));
                    else
                        obj[get_string(string_pool, str_idx)] = tv;
                }
                break;
            case LOAD_ITEM:
                {
                    type_and_value itv = stack_pop(stack, ptr);
                    type_and_value otv = stack_pop(stack, ptr);
                    check_types(otv, (1 << OBJECT) | (1 << ARRAY));
                    if (otv.t == OBJECT)
                    {
                        check_type(itv, STRING);
                        obj_def & obj = otv.v.o->value;
                        auto p = obj.find(itv.v.s->value);
                        if (p == obj.end()) stack.push_back({NIL});
                        else stack.push_back(p->second);
                    }
                    else
                    {
                        check_type(itv, INT);
                        arr_def & arr = otv.v.a->value;
                        int64_t idx = itv.v.i >= 0 ? itv.v.i : arr.size() + itv.v.i;
                        if (idx < 0 || idx >= arr.size())
                            throw vm_error("Array index (%lld) out of bound", idx);
                        stack.push_back(arr[idx]);
                    }
                }
                break;
            case STORE_ITEM:
                {
                    type_and_value tv = stack_pop(stack, ptr);
                    type_and_value itv = stack_pop(stack, ptr);
                    type_and_value otv = stack_pop(stack, ptr);
                    check_types(otv, (1 << OBJECT) | (1 << ARRAY));
                    if (otv.t == OBJECT)
                    {
                        check_type(itv, STRING);
                        obj_def & obj = otv.v.o->value;
                        if (tv.t == NIL)
                            obj.erase(itv.v.s->value);
                        else
                            obj[itv.v.s->value] = tv;
                    }
                    else
                    {
                        check_type(itv, INT);
                        arr_def & arr = otv.v.a->value;
                        int64_t idx = itv.v.i >= 0 ? itv.v.i : arr.size() + itv.v.i;
                        if (idx < 0 || idx >= arr.size())
                            throw vm_error("Array index (%lld) out of bound", idx);
                        arr[idx] = tv;
                    }
                }
                break;
            case PUSH_BINT:
                {
                    int8_t i = code_next(code, pc);
                    type_and_value tv{INT, {.i = i}};
                    stack.push_back(tv);
                }
                break;
            case PUSH_WINT:
                {
                    int16_t i = (uint16_t)code_next(code, pc);
                    i |= (uint16_t)code_next(code, pc) << 8;
                    type_and_value tv{INT, {.i = i}};
                    stack.push_back(tv);
                }
                break;
            case PUSH_DWINT:
                {
                    int32_t i = 0;
                    for (int n = 0; n < 4; n++)
                        i |= (uint32_t)code_next(code, pc) << (8 * n);
                    type_and_value tv{INT, {.i = i}};
                    stack.push_back(tv);
                }
                break;
            case PUSH_INT:
                {
                    int64_t i = 0;
                    for (int n = 0; n < 8; n++)
                        i |= (uint64_t)code_next(code, pc) << (8 * n);
                    type_and_value tv{INT, {.i = i}};
                    stack.push_back(tv);
                }
                break;
            case PUSH_FLOAT:
                {
                    uint64_t i = 0;
                    for (int n = 0; n < 8; n++)
                        i |= (uint64_t)code_next(code, pc) << (8 * n);
                    type_and_value tv{FLOAT, {.f = *(double *)&i}};
                    stack.push_back(tv);
                }
                break;
            case PUSH_STRING:
                {
                    uint8_t str_idx = code_next(code, pc);
                    stack.push_back(new_string(get_string(string_pool, str_idx)));
                }
                break;
            case PUSH_CLOSURE:
                {
                    uint8_t addr = code_next(code, pc);
                    stack.push_back(new_closure(cur_info->c_info, cur_info->s, addr));
                }
                break;
            case PUSH_ARG:
                {
                    uint8_t arg_idx = code_next(code, pc);
                    if (arg_idx < 0)
                        throw vm_error("Trying to get argument with negative index %d", arg_idx);
                    if (arg_idx < cur_info->param_count)
                        stack.push_back(stack.at(ptr - cur_info->param_count + arg_idx));
                    else
                        stack.push_back({NIL});
                }
                break;
            case PUSH_SELF:
                stack.push_back(cur_info->c_info->value.self);
                break;
            case PUSH_SUPER:
                {
                    int level = code_next(code, pc);
                    closure_info * c_info = cur_info->c_info;
                    for (int i = 0; i < level + 1; i++)
                    {
                        c_info = c_info->value.super;
                        if (!c_info)
                            throw vm_error("Trying to get level %d super closure which does not exist", level);
                    }
                    stack.push_back(c_info->value.self);
                }
                break;
            case NEW_ARRAY:
                {
                    uint8_t cnt = code_next(code, pc);
                    if (stack.size() - cnt < ptr)
                        throw vm_error("Current stack frame empty");
                    type_and_value tv = new_array(stack.cend() - cnt, stack.cend());
                    stack.resize(stack.size() - cnt);
                    stack.push_back(tv);
                }
                break;
            case POP:
                stack_pop(stack, ptr);
                break;
            case DUP:
                stack.push_back(stack_top(stack, ptr));
                break;
            case ADD:
                {
                    type_and_value tv2 = stack_pop(stack, ptr);
                    type_and_value & tv = stack_top(stack, ptr);
                    if (tv.t != tv2.t || tv.t != INT && tv.t != FLOAT && tv.t != STRING)
                        throw op_type_error("+", tv.t, tv2.t);
                    if (tv.t == INT) tv.v.i += tv2.v.i;
                    else if (tv.t == FLOAT) tv.v.f += tv2.v.f;
                    else tv = new_string(tv.v.s->value + tv2.v.s->value);
                }
                break;
            case SUB:
                {
                    type_and_value tv2 = stack_pop(stack, ptr);
                    type_and_value & tv = stack_top(stack, ptr);
                    if (tv.t != tv2.t || tv.t != INT && tv.t != FLOAT)
                        throw op_type_error("-", tv.t, tv2.t);
                    if (tv.t == INT) tv.v.i -= tv2.v.i;
                    else tv.v.f -= tv2.v.f;
                }
                break;
            case MUL:
                {
                    type_and_value tv2 = stack_pop(stack, ptr);
                    type_and_value & tv = stack_top(stack, ptr);
                    if (tv.t != tv2.t || tv.t != INT && tv.t != FLOAT)
                        throw op_type_error("*", tv.t, tv2.t);
                    if (tv.t == INT) tv.v.i *= tv2.v.i;
                    else tv.v.f *= tv2.v.f;
                }
                break;
            case DIV:
                {
                    type_and_value tv2 = stack_pop(stack, ptr);
                    type_and_value & tv = stack_top(stack, ptr);
                    if (tv.t != tv2.t || tv.t != INT && tv.t != FLOAT)
                        throw op_type_error("/", tv.t, tv2.t);
                    if (tv.t == INT) tv.v.i /= tv2.v.i;
                    else tv.v.f /= tv2.v.f;
                }
                break;
            case REM:
                {
                    type_and_value tv2 = stack_pop(stack, ptr);
                    type_and_value & tv = stack_top(stack, ptr);
                    if (tv.t != tv2.t || tv.t != INT)
                        throw op_type_error("%", tv.t, tv2.t);
                    tv.v.i %= tv2.v.i;
                }
                break;
            case POS:
                {
                    type_and_value & tv = stack_top(stack, ptr);
                    check_types(tv, (1 << INT) | (1 << FLOAT));
                }
                break;
            case NEG:
                {
                    type_and_value & tv = stack_top(stack, ptr);
                    check_types(tv, (1 << INT) | (1 << FLOAT));
                    if (tv.t == INT) tv.v.i = -tv.v.i;
                    else tv.v.f = -tv.v.f;
                }
                break;
            case BAND:
                {
                    type_and_value tv2 = stack_pop(stack, ptr);
                    type_and_value & tv = stack_top(stack, ptr);
                    if (tv.t != INT || tv2.t != INT)
                        throw op_type_error("&", tv.t, tv2.t);
                    tv.v.i &= tv2.v.i;
                }
                break;
            case BOR:
                {
                    type_and_value tv2 = stack_pop(stack, ptr);
                    type_and_value & tv = stack_top(stack, ptr);
                    if (tv.t != INT || tv2.t != INT)
                        throw op_type_error("|", tv.t, tv2.t);
                    tv.v.i |= tv2.v.i;
                }
                break;
            case BXOR:
                {
                    type_and_value tv2 = stack_pop(stack, ptr);
                    type_and_value & tv = stack_top(stack, ptr);
                    if (tv.t != INT || tv2.t != INT)
                        throw op_type_error("^", tv.t, tv2.t);
                    tv.v.i ^= tv2.v.i;
                }
                break;
            case BINV:
                {
                    type_and_value & tv = stack_top(stack, ptr);
                    check_type(tv, INT);
                    tv.v.i = ~tv.v.i;
                }
                break;
            case SHL:
                {
                    type_and_value tv2 = stack_pop(stack, ptr);
                    type_and_value & tv = stack_top(stack, ptr);
                    if (tv.t != INT || tv2.t != INT)
                        throw op_type_error("<<", tv.t, tv2.t);
                    tv.v.i <<= tv2.v.i;
                }
                break;
            case SHR:
                {
                    type_and_value tv2 = stack_pop(stack, ptr);
                    type_and_value & tv = stack_top(stack, ptr);
                    if (tv.t != INT || tv2.t != INT)
                        throw op_type_error(">>", tv.t, tv2.t);
                    tv.v.i >>= tv2.v.i;
                }
                break;
            case USHR:
                {
                    type_and_value tv2 = stack_pop(stack, ptr);
                    type_and_value & tv = stack_top(stack, ptr);
                    if (tv.t != INT || tv2.t != INT)
                        throw op_type_error(">>>", tv.t, tv2.t);
                    tv.v.i = (uint64_t)tv.v.i >> tv2.v.i;
                }
                break;
            case CMP_EQ:
                {
                    type_and_value tv2 = stack_pop(stack, ptr);
                    type_and_value tv = stack_pop(stack, ptr);
                    stack.push_back({BOOL, {.b = is_equal(tv, tv2)}});
                }
                break;
            case CMP_NE:
                {
                    type_and_value tv2 = stack_pop(stack, ptr);
                    type_and_value tv = stack_pop(stack, ptr);
                    stack.push_back({BOOL, {.b = !is_equal(tv, tv2)}});
                }
                break;
            case CMP_GT:
                {
                    type_and_value tv2 = stack_pop(stack, ptr);
                    type_and_value tv = stack_pop(stack, ptr);
                    stack.push_back({BOOL, {.b = is_greater(tv, tv2)}});
                }
                break;
            case CMP_LT:
                {
                    type_and_value tv2 = stack_pop(stack, ptr);
                    type_and_value tv = stack_pop(stack, ptr);
                    stack.push_back({BOOL, {.b = is_less(tv, tv2)}});
                }
                break;
            case CMP_GE:
                {
                    type_and_value tv2 = stack_pop(stack, ptr);
                    type_and_value tv = stack_pop(stack, ptr);
                    stack.push_back({BOOL, {.b = !is_less(tv, tv2)}});
                }
                break;
            case CMP_LE:
                {
                    type_and_value tv2 = stack_pop(stack, ptr);
                    type_and_value tv = stack_pop(stack, ptr);
                    stack.push_back({BOOL, {.b = !is_greater(tv, tv2)}});
                }
                break;
            case NOT:
                {
                    type_and_value & tv = stack_top(stack, ptr);
                    check_type(tv, BOOL);
                    tv.v.b = !tv.v.b;
                }
                break;
            case LEN:
                {
                    type_and_value tv = stack_pop(stack, ptr);
                    type_and_value ltv{INT};
                    switch (tv.t)
                    {
                    case STRING: ltv.v.i = tv.v.s->value.size(); break;
                    case OBJECT: ltv.v.i = tv.v.o->value.size(); break;
                    case ARRAY: ltv.v.i = tv.v.a->value.size(); break;
                    default: throw vm_error("Cannot apply '#' on type %s", type_name(tv.t));
                    }
                    stack.push_back(ltv);
                }
                break;
            case JUMP:
                pc += (int8_t)code_next(code, pc);
                break;
            case JUMP_IF:
                {
                    type_and_value tv = stack_pop(stack, ptr);
                    check_type(tv, BOOL);
                    int8_t offset = (int8_t)code_next(code, pc);
                    if (tv.v.b) pc += offset;
                }
                break;
            case JUMP_UNLESS:
                {
                    type_and_value tv = stack_pop(stack, ptr);
                    check_type(tv, BOOL);
                    int8_t offset = (int8_t)code_next(code, pc);
                    if (!tv.v.b) pc += offset;
                }
                break;
            case CALL:
                {
                    uint8_t arg_cnt = code_next(code, pc);
                    const type_and_value & tv = stack_top(stack, ptr, arg_cnt);
                    check_type(tv, CLOSURE);
                    const script * next_s = tv.v.c->value.s;
                    stack_info new_info
                    {
                        new_closure_info(tv.v.c->value.super, new_empty_object()),
                        next_s, arg_cnt, ptr, pc
                    };
                    info.push_back(new_info);
                    cur_info = &info.back();
                    cur_obj = &cur_info->c_info->value.self.v.o->value;
                    code = &next_s->code;
                    string_pool = &next_s->string_pool;
                    pc = tv.v.c->value.addr;
                    ptr = stack.size();
                }
                break;
            case RETURN:
                {
                    if (stack.size() - 1 != ptr)
                        throw vm_error("Incorrect stack top position");
                    type_and_value tv = stack.back();
                    stack.resize(stack.size() - cur_info->param_count - 2);
                    stack.push_back(tv);
                    if (info.size() <= 1)
                        goto cleanup;
                    pc = cur_info->pc_return;
                    ptr = cur_info->stack_return;
                    info.pop_back();
                    cur_info = &info.back();
                    cur_obj = &cur_info->c_info->value.self.v.o->value;
                    code = &cur_info->s->code;
                    string_pool = &cur_info->s->string_pool;
                    gc(stack, info);
                }
                break;
            case IN:
                {
                    static char buf[1024];
                    if (scanf("%1023s", buf) <= 0)
                        throw vm_error("Failed to read from stdin");
                    stack.push_back(new_string(buf));
                }
                break;
            case OUT:
                {
                    type_and_value tv = stack_pop(stack, ptr);
                    type_and_value stv = value_to_string(tv);
                    puts(stv.v.s->value.c_str());
                }
                break;
            case LOAD_LIB:
                {
                    uint8_t str_idx = code_next(code, pc);
                    const std::string & str = get_string(string_pool, str_idx);
                    auto p = libs.find(str);
                    if (p == libs.end())
                    {
                        // TODO: load library dynamically
                        throw vm_error("Unknown library %s", str.c_str());
                    }
                    else
                        stack.push_back(p->second);
                }
                break;
            default:
                throw vm_error("Unknown instruction %d", (*code)[pc - 1]);
            }
        }
    }
    catch (vm_error & e)
    {
        e.print();
    }
    cleanup:
    cleanup();
}

void dump_code(const script & s)
{
    static const char * names[] =
    {
        "LOAD",
        "STORE",
        "LOAD_SUPER",
        "STORE_SUPER",
        "LOAD_FIELD",
        "STORE_FIELD",
        "LOAD_ITEM",
        "STORE_ITEM",
        "PUSH_BINT",
        "PUSH_WINT",
        "PUSH_DWINT",
        "PUSH_INT",
        "PUSH_FLOAT",
        "PUSH_STRING",
        "PUSH_CLOSURE",
        "PUSH_ARG",
        "PUSH_SELF",
        "PUSH_SUPER",
        "NEW_ARRAY",
        "POP",
        "DUP",
        "ADD",
        "SUB",
        "MUL",
        "DIV",
        "REM",
        "POS",
        "NEG",
        "BAND",
        "BOR",
        "BXOR",
        "BINV",
        "SHL",
        "SHR",
        "USHR",
        "CMP_EQ",
        "CMP_NE",
        "CMP_GT",
        "CMP_LT",
        "CMP_GE",
        "CMP_LE",
        "NOT",
        "LEN",
        "JUMP",
        "JUMP_IF",
        "JUMP_UNLESS",
        "CALL",
        "RETURN",
        "IN",
        "OUT",
        "LOAD_LIB",
    };
    auto & codes = s.code;
    auto & string_pool = s.string_pool;
    size_t idx = 0;
    while (idx < codes.size())
    {
        printf("%llu ", idx);
        uint8_t code = codes.at(idx++);
        switch (code)
        {
        case LOAD_ITEM:
        case STORE_ITEM:
        case PUSH_SELF:
        case POP:
        case DUP:
        case ADD:
        case SUB:
        case MUL:
        case DIV:
        case REM:
        case POS:
        case NEG:
        case BAND:
        case BOR:
        case BXOR:
        case BINV:
        case SHL:
        case SHR:
        case USHR:
        case CMP_EQ:
        case CMP_NE:
        case CMP_GT:
        case CMP_LT:
        case CMP_GE:
        case CMP_LE:
        case NOT:
        case LEN:
        case RETURN:
        case IN:
        case OUT:
            puts(names[code]);
            break;
        case LOAD:
        case STORE:
        case LOAD_SUPER:
        case STORE_SUPER:
        case LOAD_FIELD:
        case STORE_FIELD:
        case PUSH_STRING:
        case LOAD_LIB:
            printf("%s %s\n", names[code], string_pool.at(codes.at(idx++)).c_str());
            break;
        case PUSH_BINT:
        case JUMP:
        case JUMP_IF:
        case JUMP_UNLESS:
            printf("%s %d\n", names[code], (int8_t)codes.at(idx++));
            break;
        case PUSH_WINT:
            {
                int16_t i = (uint16_t)codes.at(idx++);
                i |= (uint16_t)codes.at(idx++) << 8;
                printf("%s %d\n", names[code], i);
            }
            break;
        case PUSH_DWINT:
            {
                int32_t i = 0;
                for (int n = 0; n < 4; n++)
                    i |= (uint32_t)codes.at(idx++) << (8 * n);
                printf("%s %d\n", names[code], i);
            }
            break;
        case PUSH_INT:
            {
                int64_t i = 0;
                for (int n = 0; n < 8; n++)
                    i |= (uint64_t)codes.at(idx++) << (8 * n);
                printf("%s %lld\n", names[code], i);
            }
            break;
        case PUSH_FLOAT:
            {
                uint64_t i = 0;
                for (int n = 0; n < 8; n++)
                    i |= (uint64_t)codes.at(idx++) << (8 * n);
                printf("%s %f\n", names[code], *(double *)&i);
            }
            break;
        case PUSH_CLOSURE:
        case PUSH_ARG:
        case PUSH_SUPER:
        case NEW_ARRAY:
        case CALL:
            printf("%s %u\n", names[code], codes.at(idx++));
            break;
        default:
            printf("[Unknown: %u]\n", code);
        }
    }
}

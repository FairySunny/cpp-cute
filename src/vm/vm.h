#include <vector>
#include <string>
#include <unordered_map>

struct gc_base_obj
{
    char gc_status;
    virtual ~gc_base_obj() {}
};

template<typename T>
struct gc_obj : gc_base_obj
{
    T value;
    template<typename ... Args>
    gc_obj(Args ... args): value(args ...) {}
};

struct script
{
    std::vector<uint8_t> code;
    std::vector<std::string> string_pool;
};

struct type_and_value;

typedef const std::string str_def;
typedef gc_obj<str_def> str;
typedef std::unordered_map<std::string, type_and_value> obj_def;
typedef gc_obj<obj_def> obj;
typedef std::vector<type_and_value> arr_def;
typedef gc_obj<arr_def> arr;
struct closure_def;
typedef gc_obj<closure_def> closure;
struct closure_info_def;
typedef gc_obj<closure_info_def> closure_info;

enum type {NIL, INT, FLOAT, BOOL, STRING, OBJECT, ARRAY, CLOSURE};

struct type_and_value
{
    type t;
    union
    {
        int64_t i;
        double f;
        bool b;
        str * s;
        obj * o;
        arr * a;
        closure * c;
    } v;
};

struct closure_def
{
    closure_info * super;
    const script * s;
    int addr;
};

struct closure_info_def
{
    closure_info * super;
    type_and_value self;
};

enum instruction : uint8_t
{
    LOAD, // string (push)
    STORE, // string (pop)
    LOAD_SUPER, // string (push)
    STORE_SUPER, // string (pop)
    LOAD_FIELD, // string (pop push)
    STORE_FIELD, // string (pop pop)
    LOAD_ITEM, // (pop pop push)
    STORE_ITEM, // (pop pop pop)
    PUSH_BINT, // byte (push)
    PUSH_WINT, // word (push)
    PUSH_DWINT, // dword (push)
    PUSH_INT, // int (push)
    PUSH_FLOAT, // float (push)
    PUSH_STRING, // string (push)
    PUSH_CLOSURE, // ubyte (push)
    PUSH_ARG, // ubyte (push)
    PUSH_SELF, // (push)
    PUSH_SUPER, // ubyte (push)
    NEW_ARRAY, // ubyte (pop*n push)
    POP, // (pop)
    DUP, // (pop push push)
    ADD,
    SUB,
    MUL,
    DIV,
    REM,
    POS,
    NEG,
    BAND,
    BOR,
    BXOR,
    BINV,
    SHL,
    SHR,
    USHR,
    CMP_EQ,
    CMP_NE,
    CMP_GT,
    CMP_LT,
    CMP_GE,
    CMP_LE,
    NOT,
    LEN,
    JUMP, // byte
    JUMP_IF, // byte (pop)
    JUMP_UNLESS, // byte (pop)
    CALL, // ubyte (pop)
    RETURN, // (pop pop*n push)
    IN, // (push)
    OUT, // (pop)
    LOAD_LIB, // string (push)
};

type_and_value new_string(const std::string & str);
type_and_value new_empty_object();
type_and_value new_array(arr_def::const_iterator begin, arr_def::const_iterator end);
type_and_value new_closure(closure_info * super, const script * s, int addr);
closure_info * new_closure_info(closure_info * super, const type_and_value & self);

void run_script(const script & s);
void dump_code(const script & s);

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
    JUMP, // byte
    JUMP_IF, // byte (pop)
    JUMP_UNLESS, // byte (pop)
    CALL, // ubyte (pop)
    RETURN, // (pop pop*n push)
    IN, // (push)
    OUT, // (pop)
    LOAD_LIB, // string (push)
};

struct script
{
    std::vector<uint8_t> code;
    std::vector<std::string> string_pool;
};

void run_script(const script & s);
void dump_code(const script & s);

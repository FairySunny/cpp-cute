struct lval
{
    enum type {VAR, SUPER, FIELD, ITEM};
    type t;
    char * s;
};

struct super
{
    int level;
    char * s;
};

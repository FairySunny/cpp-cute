%{

#include <cstdio>
#include "types.h"
#include "cute.tab.h"

%}

%x SINGLE_COMMENT
%x MULTI_COMMENT

%%

<INITIAL>{
"//"|"#!"   { BEGIN(SINGLE_COMMENT); }
"/*"        { BEGIN(MULTI_COMMENT); }
}
<SINGLE_COMMENT>{
.+
\r\n?|\n    { BEGIN(INITIAL); }
}
<MULTI_COMMENT>{
"*/"        { BEGIN(INITIAL); }
[^*]+
"*"
}

[ \t\r\n]+

"("|")"|"{"|"}"|"["|"]"|";"|","|"="|"?"|":"|"." { return yytext[0]; }
"+"|"-"|"*"|"/"|"%"|">"|"<"|"!"|"&"|"|"|"^"|"~" { return yytext[0]; }
"@"|"$" { return yytext[0]; }

"=="    { return OP_EQ; }
"!="    { return OP_NE; }
">="    { return OP_GE; }
"<="    { return OP_LE; }
"&&"    { return OP_AND; }
"||"    { return OP_OR; }
"<<"    { return OP_SHL; }
">>"    { return OP_SHR; }
">>>"   { return OP_USHR; }

[1-9][0-9]* {
    yylval.i = strtoll(yytext, nullptr, 0);
    return INT_CONST;
}

0[0-7]* {
    yylval.i = strtoll(yytext, nullptr, 0);
    return INT_CONST;
}

0[Xx][0-9A-Fa-f]+ {
    yylval.i = strtoll(yytext, nullptr, 0);
    return INT_CONST;
}

0[Bb][01]+ {
    yylval.i = strtoll(yytext, nullptr, 0);
    return INT_CONST;
}

((0|[1-9][0-9]*)(\.[0-9]*)?|\.[0-9]+)([Ee][+-]?(0|[1-9][0-9]*))? {
    yylval.f = strtod(yytext, nullptr);
    return FLOAT_CONST;
}

\"([^\\\"]|\\.)*\"|'([^\\']|\\.)*' {
    int len = strlen(yytext) - 2;
    yylval.s = new char[len + 1];
    int j = 0;
    for (int i = 1; i <= len; i++, j++)
    {
        if (yytext[i] == '\\')
        {
            i++;
            switch (yytext[i])
            {
            case 'a': yylval.s[j] = '\a'; break;
            case 'b': yylval.s[j] = '\b'; break;
            case 'f': yylval.s[j] = '\f'; break;
            case 'n': yylval.s[j] = '\n'; break;
            case 'r': yylval.s[j] = '\r'; break;
            case 't': yylval.s[j] = '\t'; break;
            case 'v': yylval.s[j] = '\v'; break;
            case '\'': yylval.s[j] = '\''; break;
            case '"': yylval.s[j] = '"'; break;
            case '\\': yylval.s[j] = '\\'; break;
            default: yylval.s[j] = ' ';
            }
        }
        else yylval.s[j] = yytext[i];
    }
    yylval.s[j] = 0;
    return STRING_CONST;
}

[A-Za-z_][0-9A-Za-z_]* {
    yylval.s = new char[strlen(yytext) + 1];
    strcpy(yylval.s, yytext);
    return NAME;
}

. {
    return YYUNDEF;
}

%%

int yywrap()
{
    return 1;
}

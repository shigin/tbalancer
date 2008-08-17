%{
#include "configy.h"
%}

%option noyywrap
unumber ([0-9]+)
hostname ([A-Za-z0-9.-]+)

%%

"{"         { return LBRACER; }
"}"         { return RBRACER; }
";"         { return SEMICOLON; }
"backends"  { return BACKENDS; }
"server"    { return SERVER; }
"port"      { return PORT; }
"listen"    { return LISTEN; }
"timeout"   { return TIMEOUT; }
"ms"        { return MILISEC; }
"msec"      { return MILISEC; }
"s"         { return SEC; }
"sec"       { return SEC; }
{unumber}   { yylval.num = atoi(yytext); return NUM; }
{hostname}  { yylval.id = strdup(yytext); return STRING; }

%%

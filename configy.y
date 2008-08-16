%{
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <event.h>
#include "tpool.h"
#include "common.h"

int yylex(void);
void yyerror(const char *message)
{
    fprintf(stderr, "error reading config: %s\n", message);
}
extern struct tb_pool *opts_pool;
extern short opts_port;
extern FILE *yyin;
%}

%token STRING
%token NUM
%token RBRACER
%token LBRACER
%token SERVER
%token PORT
%token BACKENDS
%token SEMICOLON
%token LISTEN

%union {
    char *id;
    short num;
}

%%

config: 
    param config |
    pool config |
    /* empty */
;

param: 
    LISTEN NUM SEMICOLON 
    { printf("listen on port %d\n", $<num>2); opts_port = $<num>2; }
;

pool: BACKENDS begin servers end { tb_debug("backends"); }
;

begin: LBRACER 
    {
        tb_debug("backends start");
        if (opts_pool != 0) 
        {
            fprintf(stderr, "only one pool is allowed");
            YYABORT;
        }
        opts_pool = make_pool(); 
    }
end: RBRACER SEMICOLON

servers: server serverlist 
;

serverlist: server serverlist |
    /* empty */
;

server: SERVER STRING PORT NUM SEMICOLON
    {   tb_debug("add server %s port %d", $<id>2, $<num>4);
        add_server(opts_pool, $<id>2, $<num>4); }
;
%%

int xmain(int argc, char **argv)
{
    yyin = fopen(argv[1], "r");
    event_init();
    return yyparse();
}

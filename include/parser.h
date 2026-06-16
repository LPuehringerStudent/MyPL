#ifndef MYDB_PARSER_H
#define MYDB_PARSER_H

#include "ast.h"

Program* parse(const char* source);
Expr* parse_expression(const char* source);

#endif

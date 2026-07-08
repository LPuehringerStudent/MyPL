#ifndef MYDB_PARSER_H
#define MYDB_PARSER_H

#include <stddef.h>

#include "ast.h"

Program* parse(const char* source, char* error, size_t error_size);
Program* parse_with_path(const char* source, const char* path, char* error, size_t error_size);
Expr* parse_expression(const char* source);

#endif

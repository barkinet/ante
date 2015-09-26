#ifndef INTERPRETER_H
#define INTERPRETER_H

#include <stdio.h>
#include "parser.h"
#include "stack.h"
#include "expression.h"
#include "scanline.h"

#define VERSION "v0.0.13"
#define VERDATE "2015-09-22"

Token*toks;
int tIndex;

#define ERR_TYPE_MISMATCH "Attempted to set %s to an incompatible type.\n"
#define ERR_NOT_INITIALIZED "%s has not been initialized.\n"
#define ERR_ALREADY_INITIALIZED "%s has already been initialized.\n"
#define ERR_UNINITIALIZED_VALUE_IN_EXPRESSION "Attempted to use uninitialized value %s in expression.\n"

#define runtimeError(x,y) {fprintf(stderr,x,y); return;}

#define CPY_TO_STR(newStr, cpyStr) { int len=strlen(cpyStr); newStr=realloc(newStr,len+1); strcpy(newStr, cpyStr);}
#define CPY_TO_NEW_STR(newStr, cpyStr) char*newStr=NULL; CPY_TO_STR(newStr,cpyStr);
#define INC_POS(x) (tIndex += x)

void interpret(FILE*, char);
Coords lookupVar(char*);
Variable getValue(Token);

void op_initObject(void);
void op_assign(void);
void op_print(void);
void op_callFunc(void);
void op_declFunc(void);
void op_function(void);
void op_initNum(void);
void op_initInt(void);
void op_initStr(void);
void op_typeOf(void);

#endif


%skeleton "lalr1.cc" /* -*- C++ -*- */
%require "3.0"
%defines
%define api.parser.class { ZfxParser }

%define api.token.constructor
%define api.value.type variant
%define parse.assert
%define api.namespace { zeno }

%code requires
{
    #include <iostream>
    #include <string>
    #include <vector>
    #include <cmath>
    #include <cstdlib>
    #include <ctime>
    #include <vector>
    #include <memory>
    #include <zeno/formula/syntax_tree.h>

    using namespace std;

    namespace zeno {
        class ZfxScanner;
        class ZfxExecute;
    }
}

// Bison calls yylex() function that must be provided by us to suck tokens
// from the scanner. This block will be placed at the beginning of IMPLEMENTATION file (cpp).
// We define this function here (function! not method).
// This function is called only inside Bison, so we make it static to limit symbol visibility for the linker
// to avoid potential linking conflicts.
%code top
{
    #include <iostream>
    #include "zfxscanner.h"
    #include "zfxparser.hpp"
    #include <zeno/formula/zfxexecute.h>
    #include <zeno/formula/syntax_tree.h>
    #include "location.hh"

    static zeno::ZfxParser::symbol_type yylex(zeno::ZfxScanner &scanner, zeno::ZfxExecute &driver) {
        return scanner.get_next_token();
    }

    using namespace zeno;
}

/*定义parser传给scanner的参数*/
%lex-param { zeno::ZfxScanner &scanner }
%lex-param { zeno::ZfxExecute &driver }

/*定义driver传给parser的参数*/
%parse-param { zeno::ZfxScanner &scanner }
%parse-param { zeno::ZfxExecute &driver }

%locations
%define parse.trace
%define parse.error verbose

/*通过zeno::ZfxParser::make_XXX(loc)给token添加前缀*/
%define api.token.prefix {TOKEN_}

%token RPAREN
%token <string>IDENTIFIER
%token <float>NUMBER
%token <bool>TRUE
%token <bool>FALSE
%token EOL
%token END 0
%token FRAME
%token FPS
%token PI
%token COMMA
%token LITERAL
%token UNCOMPSTR
%token DOLLAR
%token DOLLARVARNAME
%token COMPARE
%token QUESTION
%token COLON
%token ZFXVAR
%token LBRACKET
%token RBRACKET
%token DOT
%token VARNAME
%token SEMICOLON
%token EQUALTO
%token IF
%token FOR
%token WHILE
%token AUTOINC
%token AUTODEC
%token LSQBRACKET
%token RSQBRACKET
%token ADDASSIGN
%token MULASSIGN
%token SUBASSIGN
%token DIVASSIGN
%token RETURN
%token CONTINUE
%token BREAK
%token TYPE
%token ATTRAT

%left ADD "+"
%left SUB "-"
%left MUL "*"
%left DIV "/"

%nonassoc NEG // 负号具有最高优先级但没有结合性

%left <string>LPAREN

%type <std::shared_ptr<ZfxASTNode>> general-statement declare-statement jump-statement code-block assign-statement array-or-exp for-condition for-step exp-statement if-statement arrcontent compareexp zfx-program multi-statements factor term func-content zenvar array-stmt for-begin for-statement
%type <std::vector<std::shared_ptr<ZfxASTNode>>> funcargs arrcontents
%type <operatorVals> assign-op
%type <string> LITERAL UNCOMPSTR DOLLAR DOLLARVARNAME RPAREN COMPARE QUESTION ZFXVAR LBRACKET RBRACKET DOT COLON TYPE ATTRAT VARNAME SEMICOLON EQUALTO IF FOR WHILE AUTOINC AUTODEC LSQBRACKET RSQBRACKET ADDASSIGN MULASSIGN SUBASSIGN DIVASSIGN RETURN CONTINUE BREAK
%type <bool> array-mark bool-stmt

%start zfx-program

%%

zfx-program: END {
            std::cout << "END" << std::endl;
            $$ = driver.makeNewNode(CODEBLOCK, DEFAULT_FUNCVAL, {});
            driver.setASTResult($$);
        }
    | multi-statements zfx-program {
            addChild($2, $1);
            $$ = $2;
        }
    ;

multi-statements: %empty {
            $$ = driver.makeNewNode(CODEBLOCK, DEFAULT_FUNCVAL, {});
        }
    | general-statement multi-statements {
            addChild($2, $1);
            $$ = $2;
        }
    ;

general-statement: declare-statement SEMICOLON { $$ = $1; }
    | assign-statement SEMICOLON { $$ = $1; }
    | if-statement { $$ = $1; }
    | for-statement { $$ = $1; }
    | jump-statement SEMICOLON { $$ = $1; }
    | exp-statement SEMICOLON { $$ = $1; }      /*可参与四则运算，以及普通的函数调用。*/
    ;

array-or-exp: exp-statement { $$ = $1; }
    | array-stmt { $$ = $1; }
    ;

assign-op: EQUALTO { $$ = AssignTo; }
    | ADDASSIGN { $$ = AddAssign; }
    | MULASSIGN { $$ = MulAssign; }
    | SUBASSIGN { $$ = SubAssign; }
    | DIVASSIGN { $$ = DivAssign; }
    ;

bool-stmt: TRUE { $$ = true; }
    | FALSE { $$ = false; }
    ;

assign-statement: zenvar assign-op array-or-exp {
            std::vector<std::shared_ptr<ZfxASTNode>> children({$1, $3});
            $$ = driver.makeNewNode(ASSIGNMENT, $2, children);
        }
    ;

jump-statement: BREAK { $$ = driver.makeNewNode(JUMP, JUMP_BREAK, {}); }
    | RETURN   { $$ = driver.makeNewNode(JUMP, JUMP_RETURN, {}); }
    | CONTINUE { $$ = driver.makeNewNode(JUMP, JUMP_CONTINUE, {}); }
    ;

arrcontent: exp-statement { $$ = $1; }
    | array-stmt { $$ = $1; }
    ;

arrcontents: arrcontent            { $$ = std::vector<std::shared_ptr<ZfxASTNode>>({$1}); }
    | arrcontents COMMA arrcontent { $1.push_back($3); $$ = $1; }
    ;

array-stmt: LBRACKET arrcontents RBRACKET { 
        $$ = driver.makeNewNode(ARRAY, DEFAULT_FUNCVAL, $2);
    }
    ;

array-mark: %empty { $$ = false; }
    | LSQBRACKET RSQBRACKET { $$ = true; }
    ;

declare-statement: TYPE VARNAME array-mark {
                auto typeNode = driver.makeTypeNode($1, $3);
                auto nameNode = driver.makeZfxVarNode($2);
                std::vector<std::shared_ptr<ZfxASTNode>> children({typeNode, nameNode});
                $$ = driver.makeNewNode(DECLARE, DEFAULT_FUNCVAL, children);
            }
    | TYPE VARNAME array-mark EQUALTO array-or-exp {
                auto typeNode = driver.makeTypeNode($1, $3);
                auto nameNode = driver.makeZfxVarNode($2);
                std::vector<std::shared_ptr<ZfxASTNode>> children({typeNode, nameNode, $5});
                $$ = driver.makeNewNode(DECLARE, DEFAULT_FUNCVAL, children);
            }
    ;

code-block: LBRACKET multi-statements RBRACKET { $$ = $2; }
    ;

if-statement: IF LPAREN exp-statement RPAREN code-block {
            std::vector<std::shared_ptr<ZfxASTNode>> children({$3, $5});
            $$ = driver.makeNewNode(IF, DEFAULT_FUNCVAL, children);
        }
    ;

for-begin: SEMICOLON { $$ = driver.makeEmptyNode(); }
    | declare-statement SEMICOLON { $$ = $1; }
    | assign-statement SEMICOLON { $$ = $1; }
    | exp-statement SEMICOLON { $$ = $1; }
    ;

for-condition:  SEMICOLON { $$ = driver.makeEmptyNode(); }
    | exp-statement SEMICOLON { $$ = $1; }
    ;

for-step: %empty { $$ = driver.makeEmptyNode(); }
    | exp-statement { $$ = $1; }
    | assign-statement { $$ = $1; }
    ;

for-statement: FOR LPAREN for-begin for-condition for-step RPAREN code-block {
            std::vector<std::shared_ptr<ZfxASTNode>> children({$3, $4, $5, $7});
            $$ = driver.makeNewNode(FOR, DEFAULT_FUNCVAL, children);
        }
    ;

exp-statement: compareexp           { $$ = $1; }
    | exp-statement COMPARE compareexp  {
                std::vector<std::shared_ptr<ZfxASTNode>>children({$1, $3});
                $$ = driver.makeNewNode(COMPOP, DEFAULT_FUNCVAL, children);
                $$->value = $2;
            }
    | exp-statement COMPARE compareexp QUESTION exp-statement COLON exp-statement {
                std::vector<std::shared_ptr<ZfxASTNode>> children({$1, $3});
                auto spCond = driver.makeNewNode(COMPOP, DEFAULT_FUNCVAL, children);
                spCond->value = $2;

                std::vector<std::shared_ptr<ZfxASTNode>> exps({spCond, $5, $7});
                $$ = driver.makeNewNode(CONDEXP, DEFAULT_FUNCVAL, exps);
            }
    ;

compareexp: factor              { $$ = $1; }
    | compareexp ADD factor {
                std::vector<std::shared_ptr<ZfxASTNode>> children({$1, $3});
                $$ = driver.makeNewNode(FOUROPERATIONS, PLUS, children);
            }
    | compareexp SUB factor {
                std::vector<std::shared_ptr<ZfxASTNode>> children({$1, $3});
                $$ = driver.makeNewNode(FOUROPERATIONS, MINUS, children);
            }
    ;

factor: term            { $$ = $1; }
    | factor MUL term   {
                std::vector<std::shared_ptr<ZfxASTNode>>children({$1, $3});
                $$ = driver.makeNewNode(FOUROPERATIONS, MUL, children);
            }
    | factor DIV term {
            std::vector<std::shared_ptr<ZfxASTNode>>children({$1, $3});
            $$ = driver.makeNewNode(FOUROPERATIONS, DIV, children);
        }
    ;

zenvar: DOLLARVARNAME   { $$ = driver.makeZfxVarNode($1, BulitInVar); }
    | VARNAME           { $$ = driver.makeZfxVarNode($1); }
    | ATTRAT zenvar {
            $$ = $2;
            $$->opVal = AttrMark;
        }
    | zenvar DOT VARNAME {
            $$ = driver.makeComponentVisit($1, $3);
        }
    | zenvar LSQBRACKET exp-statement RSQBRACKET {
            $$ = $1;
            $$->opVal = Indexing;
            $$->children.push_back($3);
        }
    | AUTOINC zenvar {
            $$ = $2;
            $$->opVal = AutoIncreaseFirst;
        }
    | zenvar AUTOINC {
            $$ = $1;
            $$->opVal = AutoIncreaseLast;
        }
    | AUTODEC zenvar {
            $$ = $2;
            $$->opVal = AutoDecreaseFirst;
        }
    | zenvar AUTODEC {
            $$ = $1;
            $$->opVal = AutoDecreaseLast;
        }
    ;

funcargs: %empty { $$ = std::vector<std::shared_ptr<ZfxASTNode>>(); }
    | exp-statement            { $$ = std::vector<std::shared_ptr<ZfxASTNode>>({$1}); }
    | funcargs COMMA exp-statement { $1.push_back($3); $$ = $1; }
    ;

/* 暂不考虑不完整匹配的情况 */
func-content: LPAREN funcargs RPAREN { 
        $$ = driver.makeNewNode(FUNC, DEFAULT_FUNCVAL, $2);
        $$->isParenthesisNodeComplete = true;
        $$->func_match = Match_Exactly;
    }
    ;


term: NUMBER            { $$ = driver.makeNewNumberNode($1); }
    | bool-stmt         { $$ = driver.makeBoolNode($1); }
    | LITERAL           { $$ = driver.makeStringNode($1); }
    | UNCOMPSTR         { $$ = driver.makeQuoteStringNode($1); }
    | LPAREN exp-statement RPAREN { $$ = $2; }
    | SUB exp-statement %prec NEG { $2->value = -1 * std::get<float>($2->value); $$ = $2; }
    | zenvar            { $$ = $1; }
    | VARNAME func-content  { 
        $$ = $2;
        $$->opVal = DEFAULT_FUNCVAL;
        $$->type = FUNC;
        $$->value = $1;
        $$->isParenthesisNode = true;
    }
    ;



%%

// Bison expects us to provide implementation - otherwise linker complains
void zeno::ZfxParser::error(const location &loc , const std::string &message) {
    cout << "Error: " << message << endl << "Error location: " << driver.location() << endl;
}


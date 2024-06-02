#ifndef __TREE_H__
#define __TREE_H__

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <cmath>
#include <memory>
#include <zeno/core/common.h>


enum nodeType {
    UNDEFINE = 0,
    NUMBER,             //数字
    FUNC,               //函数
    FOUROPERATIONS,     //四则运算+ - * / %
    STRING,             //字符串
    ZENVAR,
    PLACEHOLDER,
};

enum operatorVals {
    UNDEFINE_OP = 0,
    //四则运算 nodeType对应FOUROPERATIONS
    PLUS,
    MINUS,
    MUL,
    DIV,
    //函数 nodeType对应FUNC
    SIN,
    SINH,
    COS,
    COSH,
    ABS,
    DEFAULT_FUNCVAL,
};

struct node {
    enum operatorVals opVal;
    enum nodeType type;

    std::vector<std::shared_ptr<struct node>> children;
    std::weak_ptr<struct node> parent;

    //float value = 0;  //如果是number
    //std::string string_value;    //func name.
    zeno::zvariant value;

    bool isParenthesisNode = false;
    bool isParenthesisNodeComplete = false;
    bool bCompleted = false;
};

char* getOperatorString(nodeType type, operatorVals op);
operatorVals funcName2Enum(std::string func);

std::shared_ptr<struct node> newNode(nodeType type, operatorVals op, std::vector<std::shared_ptr<struct node>> Children);
std::shared_ptr<struct node> newNumberNode(float value);

void print_syntax_tree(std::shared_ptr<struct node> root, int depth);
float calc_syntax_tree(std::shared_ptr<struct node> root);

void currFuncNamePos(std::shared_ptr<struct node> root, std::string& name, int& pos);  //当前函数名及处于第几个参数
void preOrderVec(std::shared_ptr<struct node> root, std::vector<std::shared_ptr<struct node>>& tmplist);

bool checkparentheses(std::string& exp, int& addleft, int& addright);
#endif
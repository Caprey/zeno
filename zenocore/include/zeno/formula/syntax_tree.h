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
    NUMBER,             //����
    FUNC,               //����
    FOUROPERATIONS,     //��������+ - * / %
    STRING,             //�ַ���
    ZENVAR,
    COMPOP,             //������
    CONDEXP,            //�������ʽ
    ARRAY,
    MATRIX,
    COMPVISIT,          //����Ԫ�ط���������vec.x vec.y vec.z
    PLACEHOLDER,
};

enum operatorVals {
    UNDEFINE_OP = 0,
    //�������� nodeType��ӦFOUROPERATIONS
    PLUS,
    MINUS,
    MUL,
    DIV,
    //���� nodeType��ӦFUNC
    SIN,
    SINH,
    COS,
    COSH,
    ABS,
    DEFAULT_FUNCVAL,
};

enum TokenMatchCase {
    Match_Nothing,
    Match_LeftPAREN,
    Match_Exactly,      //fully match
};

struct ZfxASTNode {
    enum operatorVals opVal;
    enum nodeType type;

    std::vector<std::shared_ptr<ZfxASTNode>> children;
    std::weak_ptr<ZfxASTNode> parent;

    zeno::zvariant value;

    TokenMatchCase func_match = Match_Nothing;
    TokenMatchCase paren_match = Match_Nothing;

    bool isParenthesisNode = false;
    bool isParenthesisNodeComplete = false;
    bool bCompleted = false;
};

struct FuncContext {
    std::string nodePath;
};

std::string getOperatorString(nodeType type, operatorVals op);
operatorVals funcName2Enum(std::string func);

std::shared_ptr<ZfxASTNode> newNode(nodeType type, operatorVals op, std::vector<std::shared_ptr<ZfxASTNode>> Children);
std::shared_ptr<ZfxASTNode> newNumberNode(float value);

void print_syntax_tree(std::shared_ptr<ZfxASTNode> root, int depth, std::string& printContent);
float calc_syntax_tree(std::shared_ptr<ZfxASTNode> root);

void currFuncNamePos(std::shared_ptr<ZfxASTNode> root, std::string& name, int& pos);  //��ǰ�����������ڵڼ�������
void preOrderVec(std::shared_ptr<ZfxASTNode> root, std::vector<std::shared_ptr<ZfxASTNode>>& tmplist);

bool checkparentheses(std::string& exp, int& addleft, int& addright);
#endif
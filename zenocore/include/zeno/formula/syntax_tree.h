#ifndef __TREE_H__
#define __TREE_H__

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <cmath>
#include <memory>
#include <zeno/core/common.h>
#include <zeno/core/INode.h>
#include <glm/glm.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <zeno/types/PrimitiveObject.h>


namespace zeno {

enum nodeType {
    UNDEFINE = 0,
    NUMBER,             //����
    BOOLTYPE,
    FUNC,               //����
    FOUROPERATIONS,     //��������+ - * / %
    STRING,             //�ַ���
    ZENVAR,
    COMPOP,             //������
    CONDEXP,            //�������ʽ
    ARRAY,
    PLACEHOLDER,
    DECLARE,            //��������
    ASSIGNMENT,           //��ֵ
    IF,
    FOR,
    FOREACH,
    FOREACH_ATTR,
    EACH_ATTRS,
    WHILE,
    DOWHILE,
    CODEBLOCK,          //����﷨����Ϊchildren�Ĵ����
    JUMP,
    VARIABLETYPE,       //�������ͣ�����int vector3 float string��
};

enum operatorVals {
    UNDEFINE_OP = 0,
    DEFAULT_FUNCVAL,

    //�������� nodeType��ӦFOUROPERATIONS
    PLUS,
    MINUS,
    MUL,
    DIV,
    MOD,
    OR,
    AND,

    //���� nodeType��ӦFUNC
    SIN,
    SINH,
    COS,
    COSH,
    ABS,

    //�ȽϷ���
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Equal,
    NotEqual,

    //���½���Ա���
    AssignTo,
    AddAssign,
    MulAssign,
    SubAssign,
    DivAssign,

    JUMP_RETURN,
    JUMP_CONTINUE,
    JUMP_BREAK,

    TYPE_INT,
    TYPE_INT_ARR,   //�����һά����
    TYPE_FLOAT,
    TYPE_FLOAT_ARR,
    TYPE_STRING,
    TYPE_STRING_ARR,
    TYPE_VECTOR2,
    TYPE_VECTOR3,
    TYPE_VECTOR4,
    TYPE_MATRIX2,
    TYPE_MATRIX3,
    TYPE_MATRIX4,

    AutoIncreaseFirst,
    AutoIncreaseLast,
    AutoDecreaseFirst,
    AutoDecreaseLast,
    Indexing,
    COMPVISIT,      //a.x, a.y, a.z
    BulitInVar,     //$F, $FPS, $T
};

using zfxintarr = std::vector<int>;
using zfxfloatarr = std::vector<float>;
using zfxstringarr = std::vector<std::string>;

using zfxvariant = std::variant<int, float, std::string,
    zfxintarr, zfxfloatarr, zfxstringarr,
    glm::vec2, glm::vec3, glm::vec4, 
    glm::mat2, glm::mat3, glm::mat4>;

enum TokenMatchCase {
    Match_Nothing,
    Match_LeftPAREN,
    Match_Exactly,      //fully match
};

enum ZfxRunOver {
    RunOver_Points,
    RunOver_Face,
    RunOver_Geom,
};

struct ZfxASTNode {
    std::string code;

    enum operatorVals opVal;
    enum nodeType type;

    std::vector<std::shared_ptr<ZfxASTNode>> children;
    std::weak_ptr<ZfxASTNode> parent;

    zfxvariant value;

    TokenMatchCase func_match = Match_Nothing;
    TokenMatchCase paren_match = Match_Nothing;

    bool isParenthesisNode = false;
    bool isParenthesisNodeComplete = false;
    bool bCompleted = false;
    bool AttrAssociateVar = false;      //������ر����ĸ�ֵ������int a = @P.y,  b=  @N.x; ����Ҫ���޳���
    bool bOverridedStmt = false;        //�ᱻif�������ǵ�stmt������Ƕ�뵽foreach����ѭ���
    bool bOverridedIfLoop = false;      //�ᱻif�������ǵ�if/while/for��䡣
    bool bAttr = false;                 //����ֵ��@P @N @Cd
    int sortOrderNum = 0;               //�����Ⱥ�˳�������ֵ
};

struct ZfxContext
{
    /* in */ std::shared_ptr<IObject> spObject;
    /* in */ std::weak_ptr<INode> spNode;
    /* in */ std::string code;
    /* in */ bool bParsingAttr = false;     //��parse���﷨������Ҫ�Դ������ԵĽ��д�����������һ��foreachѭ���Ա�������
    /* in */ ZfxRunOver runover = RunOver_Points;
    /* out */ std::string printContent;
    /* out */ operatorVals jumpFlag;
};

std::string getOperatorString(nodeType type, operatorVals op);
operatorVals funcName2Enum(std::string func);

std::shared_ptr<ZfxASTNode> newNode(nodeType type, operatorVals op, std::vector<std::shared_ptr<ZfxASTNode>> Children);
std::shared_ptr<ZfxASTNode> newNumberNode(float value);
void addChild(std::shared_ptr<ZfxASTNode> spNode, std::shared_ptr<ZfxASTNode> spChild);
void appendChild(std::shared_ptr<ZfxASTNode> spNode, std::shared_ptr<ZfxASTNode> spChild);

void print_syntax_tree(std::shared_ptr<ZfxASTNode> root, int depth, std::string& printContent);
float calc_syntax_tree(std::shared_ptr<ZfxASTNode> root);

void printSyntaxTree(std::shared_ptr<ZfxASTNode> root, std::string original_code);

void currFuncNamePos(std::shared_ptr<ZfxASTNode> root, std::string& name, int& pos);  //��ǰ�����������ڵڼ�������
void preOrderVec(std::shared_ptr<ZfxASTNode> root, std::vector<std::shared_ptr<ZfxASTNode>>& tmplist);

bool checkparentheses(std::string& exp, int& addleft, int& addright);

void findAllZenVar(std::shared_ptr<ZfxASTNode> root, std::set<std::string>& vars);

int markOrder(std::shared_ptr<ZfxASTNode> root, int startIndex);

void removeAstNode(std::shared_ptr<ZfxASTNode> root);

std::shared_ptr<ZfxASTNode> clone(std::shared_ptr<ZfxASTNode> spNode);

std::string decompile(std::shared_ptr<ZfxASTNode> root, const std::string& indent = "");

}

#endif
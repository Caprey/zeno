// A Bison parser, made by GNU Bison 3.8.2.

// Skeleton implementation for Bison LALR(1) parsers in C++

// Copyright (C) 2002-2015, 2018-2021 Free Software Foundation, Inc.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// As a special exception, you may create a larger work that contains
// part or all of the Bison parser skeleton and distribute that work
// under terms of your choice, so long as that work isn't itself a
// parser generator using the skeleton or a modified version thereof
// as a parser skeleton.  Alternatively, if you modify or redistribute
// the parser skeleton itself, you may (at your option) remove this
// special exception, which will cause the skeleton and the resulting
// Bison output files to be licensed under the GNU General Public
// License without this special exception.

// This special exception was added by the Free Software Foundation in
// version 2.2 of Bison.

// DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
// especially those whose name start with YY_ or yy_.  They are
// private implementation details that can be changed or removed.

// "%code top" blocks.
#line 37 "parser.y"

    #include <iostream>
    #include "scanner.h"
    #include "parser.hpp"
    #include <zeno/formula/formula.h>
    #include <zeno/formula/syntax_tree.h>
    #include "location.hh"
    
    // yylex() arguments are defined in parser.y
    static zeno::Parser::symbol_type yylex(zeno::Scanner &scanner, zeno::Formula &driver) {
        return scanner.get_next_token();
    }
    
    // you can accomplish the same thing by inlining the code using preprocessor
    // x and y are same as in above static function
    // #define yylex(x, y) scanner.get_next_token()
    
    using namespace zeno;


#line 60 "parser.cpp"




#include "parser.hpp"




#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> // FIXME: INFRINGES ON USER NAME SPACE.
#   define YY_(msgid) dgettext ("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif


// Whether we are compiled with exception support.
#ifndef YY_EXCEPTIONS
# if defined __GNUC__ && !defined __EXCEPTIONS
#  define YY_EXCEPTIONS 0
# else
#  define YY_EXCEPTIONS 1
# endif
#endif

#define YYRHSLOC(Rhs, K) ((Rhs)[K].location)
/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

# ifndef YYLLOC_DEFAULT
#  define YYLLOC_DEFAULT(Current, Rhs, N)                               \
    do                                                                  \
      if (N)                                                            \
        {                                                               \
          (Current).begin  = YYRHSLOC (Rhs, 1).begin;                   \
          (Current).end    = YYRHSLOC (Rhs, N).end;                     \
        }                                                               \
      else                                                              \
        {                                                               \
          (Current).begin = (Current).end = YYRHSLOC (Rhs, 0).end;      \
        }                                                               \
    while (false)
# endif


// Enable debugging if requested.
#if YYDEBUG

// A pseudo ostream that takes yydebug_ into account.
# define YYCDEBUG if (yydebug_) (*yycdebug_)

# define YY_SYMBOL_PRINT(Title, Symbol)         \
  do {                                          \
    if (yydebug_)                               \
    {                                           \
      *yycdebug_ << Title << ' ';               \
      yy_print_ (*yycdebug_, Symbol);           \
      *yycdebug_ << '\n';                       \
    }                                           \
  } while (false)

# define YY_REDUCE_PRINT(Rule)          \
  do {                                  \
    if (yydebug_)                       \
      yy_reduce_print_ (Rule);          \
  } while (false)

# define YY_STACK_PRINT()               \
  do {                                  \
    if (yydebug_)                       \
      yy_stack_print_ ();                \
  } while (false)

#else // !YYDEBUG

# define YYCDEBUG if (false) std::cerr
# define YY_SYMBOL_PRINT(Title, Symbol)  YY_USE (Symbol)
# define YY_REDUCE_PRINT(Rule)           static_cast<void> (0)
# define YY_STACK_PRINT()                static_cast<void> (0)

#endif // !YYDEBUG

#define yyerrok         (yyerrstatus_ = 0)
#define yyclearin       (yyla.clear ())

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYRECOVERING()  (!!yyerrstatus_)

#line 10 "parser.y"
namespace  zeno  {
#line 160 "parser.cpp"

  /// Build a parser object.
   Parser :: Parser  (zeno::Scanner &scanner_yyarg, zeno::Formula &driver_yyarg)
#if YYDEBUG
    : yydebug_ (false),
      yycdebug_ (&std::cerr),
#else
    :
#endif
      scanner (scanner_yyarg),
      driver (driver_yyarg)
  {}

   Parser ::~ Parser  ()
  {}

   Parser ::syntax_error::~syntax_error () YY_NOEXCEPT YY_NOTHROW
  {}

  /*---------.
  | symbol.  |
  `---------*/



  // by_state.
   Parser ::by_state::by_state () YY_NOEXCEPT
    : state (empty_state)
  {}

   Parser ::by_state::by_state (const by_state& that) YY_NOEXCEPT
    : state (that.state)
  {}

  void
   Parser ::by_state::clear () YY_NOEXCEPT
  {
    state = empty_state;
  }

  void
   Parser ::by_state::move (by_state& that)
  {
    state = that.state;
    that.clear ();
  }

   Parser ::by_state::by_state (state_type s) YY_NOEXCEPT
    : state (s)
  {}

   Parser ::symbol_kind_type
   Parser ::by_state::kind () const YY_NOEXCEPT
  {
    if (state == empty_state)
      return symbol_kind::S_YYEMPTY;
    else
      return YY_CAST (symbol_kind_type, yystos_[+state]);
  }

   Parser ::stack_symbol_type::stack_symbol_type ()
  {}

   Parser ::stack_symbol_type::stack_symbol_type (YY_RVREF (stack_symbol_type) that)
    : super_type (YY_MOVE (that.state), YY_MOVE (that.location))
  {
    switch (that.kind ())
    {
      case symbol_kind::S_NUMBER: // NUMBER
        value.YY_MOVE_OR_COPY< float > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_calclist: // calclist
      case symbol_kind::S_exp: // exp
      case symbol_kind::S_factor: // factor
      case symbol_kind::S_zenvar: // zenvar
      case symbol_kind::S_funccontent: // funccontent
      case symbol_kind::S_parencontent: // parencontent
      case symbol_kind::S_term: // term
        value.YY_MOVE_OR_COPY< std::shared_ptr<ZfxASTNode> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_funcargs: // funcargs
        value.YY_MOVE_OR_COPY< std::vector<std::shared_ptr<ZfxASTNode>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_RPAREN: // RPAREN
      case symbol_kind::S_IDENTIFIER: // IDENTIFIER
      case symbol_kind::S_LITERAL: // LITERAL
      case symbol_kind::S_FUNC: // FUNC
      case symbol_kind::S_UNCOMPSTR: // UNCOMPSTR
      case symbol_kind::S_DOLLAR: // DOLLAR
      case symbol_kind::S_VARNAME: // VARNAME
      case symbol_kind::S_LPAREN: // LPAREN
        value.YY_MOVE_OR_COPY< string > (YY_MOVE (that.value));
        break;

      default:
        break;
    }

#if 201103L <= YY_CPLUSPLUS
    // that is emptied.
    that.state = empty_state;
#endif
  }

   Parser ::stack_symbol_type::stack_symbol_type (state_type s, YY_MOVE_REF (symbol_type) that)
    : super_type (s, YY_MOVE (that.location))
  {
    switch (that.kind ())
    {
      case symbol_kind::S_NUMBER: // NUMBER
        value.move< float > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_calclist: // calclist
      case symbol_kind::S_exp: // exp
      case symbol_kind::S_factor: // factor
      case symbol_kind::S_zenvar: // zenvar
      case symbol_kind::S_funccontent: // funccontent
      case symbol_kind::S_parencontent: // parencontent
      case symbol_kind::S_term: // term
        value.move< std::shared_ptr<ZfxASTNode> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_funcargs: // funcargs
        value.move< std::vector<std::shared_ptr<ZfxASTNode>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_RPAREN: // RPAREN
      case symbol_kind::S_IDENTIFIER: // IDENTIFIER
      case symbol_kind::S_LITERAL: // LITERAL
      case symbol_kind::S_FUNC: // FUNC
      case symbol_kind::S_UNCOMPSTR: // UNCOMPSTR
      case symbol_kind::S_DOLLAR: // DOLLAR
      case symbol_kind::S_VARNAME: // VARNAME
      case symbol_kind::S_LPAREN: // LPAREN
        value.move< string > (YY_MOVE (that.value));
        break;

      default:
        break;
    }

    // that is emptied.
    that.kind_ = symbol_kind::S_YYEMPTY;
  }

#if YY_CPLUSPLUS < 201103L
   Parser ::stack_symbol_type&
   Parser ::stack_symbol_type::operator= (const stack_symbol_type& that)
  {
    state = that.state;
    switch (that.kind ())
    {
      case symbol_kind::S_NUMBER: // NUMBER
        value.copy< float > (that.value);
        break;

      case symbol_kind::S_calclist: // calclist
      case symbol_kind::S_exp: // exp
      case symbol_kind::S_factor: // factor
      case symbol_kind::S_zenvar: // zenvar
      case symbol_kind::S_funccontent: // funccontent
      case symbol_kind::S_parencontent: // parencontent
      case symbol_kind::S_term: // term
        value.copy< std::shared_ptr<ZfxASTNode> > (that.value);
        break;

      case symbol_kind::S_funcargs: // funcargs
        value.copy< std::vector<std::shared_ptr<ZfxASTNode>> > (that.value);
        break;

      case symbol_kind::S_RPAREN: // RPAREN
      case symbol_kind::S_IDENTIFIER: // IDENTIFIER
      case symbol_kind::S_LITERAL: // LITERAL
      case symbol_kind::S_FUNC: // FUNC
      case symbol_kind::S_UNCOMPSTR: // UNCOMPSTR
      case symbol_kind::S_DOLLAR: // DOLLAR
      case symbol_kind::S_VARNAME: // VARNAME
      case symbol_kind::S_LPAREN: // LPAREN
        value.copy< string > (that.value);
        break;

      default:
        break;
    }

    location = that.location;
    return *this;
  }

   Parser ::stack_symbol_type&
   Parser ::stack_symbol_type::operator= (stack_symbol_type& that)
  {
    state = that.state;
    switch (that.kind ())
    {
      case symbol_kind::S_NUMBER: // NUMBER
        value.move< float > (that.value);
        break;

      case symbol_kind::S_calclist: // calclist
      case symbol_kind::S_exp: // exp
      case symbol_kind::S_factor: // factor
      case symbol_kind::S_zenvar: // zenvar
      case symbol_kind::S_funccontent: // funccontent
      case symbol_kind::S_parencontent: // parencontent
      case symbol_kind::S_term: // term
        value.move< std::shared_ptr<ZfxASTNode> > (that.value);
        break;

      case symbol_kind::S_funcargs: // funcargs
        value.move< std::vector<std::shared_ptr<ZfxASTNode>> > (that.value);
        break;

      case symbol_kind::S_RPAREN: // RPAREN
      case symbol_kind::S_IDENTIFIER: // IDENTIFIER
      case symbol_kind::S_LITERAL: // LITERAL
      case symbol_kind::S_FUNC: // FUNC
      case symbol_kind::S_UNCOMPSTR: // UNCOMPSTR
      case symbol_kind::S_DOLLAR: // DOLLAR
      case symbol_kind::S_VARNAME: // VARNAME
      case symbol_kind::S_LPAREN: // LPAREN
        value.move< string > (that.value);
        break;

      default:
        break;
    }

    location = that.location;
    // that is emptied.
    that.state = empty_state;
    return *this;
  }
#endif

  template <typename Base>
  void
   Parser ::yy_destroy_ (const char* yymsg, basic_symbol<Base>& yysym) const
  {
    if (yymsg)
      YY_SYMBOL_PRINT (yymsg, yysym);
  }

#if YYDEBUG
  template <typename Base>
  void
   Parser ::yy_print_ (std::ostream& yyo, const basic_symbol<Base>& yysym) const
  {
    std::ostream& yyoutput = yyo;
    YY_USE (yyoutput);
    if (yysym.empty ())
      yyo << "empty symbol";
    else
      {
        symbol_kind_type yykind = yysym.kind ();
        yyo << (yykind < YYNTOKENS ? "token" : "nterm")
            << ' ' << yysym.name () << " ("
            << yysym.location << ": ";
        YY_USE (yykind);
        yyo << ')';
      }
  }
#endif

  void
   Parser ::yypush_ (const char* m, YY_MOVE_REF (stack_symbol_type) sym)
  {
    if (m)
      YY_SYMBOL_PRINT (m, sym);
    yystack_.push (YY_MOVE (sym));
  }

  void
   Parser ::yypush_ (const char* m, state_type s, YY_MOVE_REF (symbol_type) sym)
  {
#if 201103L <= YY_CPLUSPLUS
    yypush_ (m, stack_symbol_type (s, std::move (sym)));
#else
    stack_symbol_type ss (s, sym);
    yypush_ (m, ss);
#endif
  }

  void
   Parser ::yypop_ (int n) YY_NOEXCEPT
  {
    yystack_.pop (n);
  }

#if YYDEBUG
  std::ostream&
   Parser ::debug_stream () const
  {
    return *yycdebug_;
  }

  void
   Parser ::set_debug_stream (std::ostream& o)
  {
    yycdebug_ = &o;
  }


   Parser ::debug_level_type
   Parser ::debug_level () const
  {
    return yydebug_;
  }

  void
   Parser ::set_debug_level (debug_level_type l)
  {
    yydebug_ = l;
  }
#endif // YYDEBUG

   Parser ::state_type
   Parser ::yy_lr_goto_state_ (state_type yystate, int yysym)
  {
    int yyr = yypgoto_[yysym - YYNTOKENS] + yystate;
    if (0 <= yyr && yyr <= yylast_ && yycheck_[yyr] == yystate)
      return yytable_[yyr];
    else
      return yydefgoto_[yysym - YYNTOKENS];
  }

  bool
   Parser ::yy_pact_value_is_default_ (int yyvalue) YY_NOEXCEPT
  {
    return yyvalue == yypact_ninf_;
  }

  bool
   Parser ::yy_table_value_is_error_ (int yyvalue) YY_NOEXCEPT
  {
    return yyvalue == yytable_ninf_;
  }

  int
   Parser ::operator() ()
  {
    return parse ();
  }

  int
   Parser ::parse ()
  {
    int yyn;
    /// Length of the RHS of the rule being reduced.
    int yylen = 0;

    // Error handling.
    int yynerrs_ = 0;
    int yyerrstatus_ = 0;

    /// The lookahead symbol.
    symbol_type yyla;

    /// The locations where the error started and ended.
    stack_symbol_type yyerror_range[3];

    /// The return value of parse ().
    int yyresult;

#if YY_EXCEPTIONS
    try
#endif // YY_EXCEPTIONS
      {
    YYCDEBUG << "Starting parse\n";


    /* Initialize the stack.  The initial state will be set in
       yynewstate, since the latter expects the semantical and the
       location values to have been already stored, initialize these
       stacks with a primary value.  */
    yystack_.clear ();
    yypush_ (YY_NULLPTR, 0, YY_MOVE (yyla));

  /*-----------------------------------------------.
  | yynewstate -- push a new symbol on the stack.  |
  `-----------------------------------------------*/
  yynewstate:
    YYCDEBUG << "Entering state " << int (yystack_[0].state) << '\n';
    YY_STACK_PRINT ();

    // Accept?
    if (yystack_[0].state == yyfinal_)
      YYACCEPT;

    goto yybackup;


  /*-----------.
  | yybackup.  |
  `-----------*/
  yybackup:
    // Try to take a decision without lookahead.
    yyn = yypact_[+yystack_[0].state];
    if (yy_pact_value_is_default_ (yyn))
      goto yydefault;

    // Read a lookahead token.
    if (yyla.empty ())
      {
        YYCDEBUG << "Reading a token\n";
#if YY_EXCEPTIONS
        try
#endif // YY_EXCEPTIONS
          {
            symbol_type yylookahead (yylex (scanner, driver));
            yyla.move (yylookahead);
          }
#if YY_EXCEPTIONS
        catch (const syntax_error& yyexc)
          {
            YYCDEBUG << "Caught exception: " << yyexc.what() << '\n';
            error (yyexc);
            goto yyerrlab1;
          }
#endif // YY_EXCEPTIONS
      }
    YY_SYMBOL_PRINT ("Next token is", yyla);

    if (yyla.kind () == symbol_kind::S_YYerror)
    {
      // The scanner already issued an error message, process directly
      // to error recovery.  But do not keep the error token as
      // lookahead, it is too special and may lead us to an endless
      // loop in error recovery. */
      yyla.kind_ = symbol_kind::S_YYUNDEF;
      goto yyerrlab1;
    }

    /* If the proper action on seeing token YYLA.TYPE is to reduce or
       to detect an error, take that action.  */
    yyn += yyla.kind ();
    if (yyn < 0 || yylast_ < yyn || yycheck_[yyn] != yyla.kind ())
      {
        goto yydefault;
      }

    // Reduce or error.
    yyn = yytable_[yyn];
    if (yyn <= 0)
      {
        if (yy_table_value_is_error_ (yyn))
          goto yyerrlab;
        yyn = -yyn;
        goto yyreduce;
      }

    // Count tokens shifted since error; after three, turn off error status.
    if (yyerrstatus_)
      --yyerrstatus_;

    // Shift the lookahead token.
    yypush_ ("Shifting", state_type (yyn), YY_MOVE (yyla));
    goto yynewstate;


  /*-----------------------------------------------------------.
  | yydefault -- do the default action for the current state.  |
  `-----------------------------------------------------------*/
  yydefault:
    yyn = yydefact_[+yystack_[0].state];
    if (yyn == 0)
      goto yyerrlab;
    goto yyreduce;


  /*-----------------------------.
  | yyreduce -- do a reduction.  |
  `-----------------------------*/
  yyreduce:
    yylen = yyr2_[yyn];
    {
      stack_symbol_type yylhs;
      yylhs.state = yy_lr_goto_state_ (yystack_[yylen].state, yyr1_[yyn]);
      /* Variants are always initialized to an empty instance of the
         correct type. The default '$$ = $1' action is NOT applied
         when using variants.  */
      switch (yyr1_[yyn])
    {
      case symbol_kind::S_NUMBER: // NUMBER
        yylhs.value.emplace< float > ();
        break;

      case symbol_kind::S_calclist: // calclist
      case symbol_kind::S_exp: // exp
      case symbol_kind::S_factor: // factor
      case symbol_kind::S_zenvar: // zenvar
      case symbol_kind::S_funccontent: // funccontent
      case symbol_kind::S_parencontent: // parencontent
      case symbol_kind::S_term: // term
        yylhs.value.emplace< std::shared_ptr<ZfxASTNode> > ();
        break;

      case symbol_kind::S_funcargs: // funcargs
        yylhs.value.emplace< std::vector<std::shared_ptr<ZfxASTNode>> > ();
        break;

      case symbol_kind::S_RPAREN: // RPAREN
      case symbol_kind::S_IDENTIFIER: // IDENTIFIER
      case symbol_kind::S_LITERAL: // LITERAL
      case symbol_kind::S_FUNC: // FUNC
      case symbol_kind::S_UNCOMPSTR: // UNCOMPSTR
      case symbol_kind::S_DOLLAR: // DOLLAR
      case symbol_kind::S_VARNAME: // VARNAME
      case symbol_kind::S_LPAREN: // LPAREN
        yylhs.value.emplace< string > ();
        break;

      default:
        break;
    }


      // Default location.
      {
        stack_type::slice range (yystack_, yylen);
        YYLLOC_DEFAULT (yylhs.location, range, yylen);
        yyerror_range[1].location = yylhs.location;
      }

      // Perform the reduction.
      YY_REDUCE_PRINT (yyn);
#if YY_EXCEPTIONS
      try
#endif // YY_EXCEPTIONS
        {
          switch (yyn)
            {
  case 2: // calclist: %empty
#line 106 "parser.y"
                {}
#line 700 "parser.cpp"
    break;

  case 3: // calclist: calclist exp EOL
#line 106 "parser.y"
                                    {
    yylhs.value.as < std::shared_ptr<ZfxASTNode> > () = yystack_[1].value.as < std::shared_ptr<ZfxASTNode> > ();
    driver.setASTResult(yylhs.value.as < std::shared_ptr<ZfxASTNode> > ());
}
#line 709 "parser.cpp"
    break;

  case 4: // exp: factor
#line 111 "parser.y"
                        { yylhs.value.as < std::shared_ptr<ZfxASTNode> > () = yystack_[0].value.as < std::shared_ptr<ZfxASTNode> > (); }
#line 715 "parser.cpp"
    break;

  case 5: // exp: exp ADD factor
#line 112 "parser.y"
                        { 
            std::vector<std::shared_ptr<ZfxASTNode>>children({yystack_[2].value.as < std::shared_ptr<ZfxASTNode> > (), yystack_[0].value.as < std::shared_ptr<ZfxASTNode> > ()});
            yylhs.value.as < std::shared_ptr<ZfxASTNode> > () = driver.makeNewNode(FOUROPERATIONS, PLUS, children);
            }
#line 724 "parser.cpp"
    break;

  case 6: // exp: exp SUB factor
#line 116 "parser.y"
                        {
            std::vector<std::shared_ptr<ZfxASTNode>>children({yystack_[2].value.as < std::shared_ptr<ZfxASTNode> > (), yystack_[0].value.as < std::shared_ptr<ZfxASTNode> > ()});
            yylhs.value.as < std::shared_ptr<ZfxASTNode> > () = driver.makeNewNode(FOUROPERATIONS, MINUS, children);
            }
#line 733 "parser.cpp"
    break;

  case 7: // factor: term
#line 122 "parser.y"
                        { yylhs.value.as < std::shared_ptr<ZfxASTNode> > () = yystack_[0].value.as < std::shared_ptr<ZfxASTNode> > (); }
#line 739 "parser.cpp"
    break;

  case 8: // factor: factor MUL term
#line 123 "parser.y"
                        { 
            std::vector<std::shared_ptr<ZfxASTNode>>children({yystack_[2].value.as < std::shared_ptr<ZfxASTNode> > (), yystack_[0].value.as < std::shared_ptr<ZfxASTNode> > ()});
            yylhs.value.as < std::shared_ptr<ZfxASTNode> > () = driver.makeNewNode(FOUROPERATIONS, MUL, children);
            }
#line 748 "parser.cpp"
    break;

  case 9: // factor: factor DIV term
#line 127 "parser.y"
                      {
            std::vector<std::shared_ptr<ZfxASTNode>>children({yystack_[2].value.as < std::shared_ptr<ZfxASTNode> > (), yystack_[0].value.as < std::shared_ptr<ZfxASTNode> > ()});
            yylhs.value.as < std::shared_ptr<ZfxASTNode> > () = driver.makeNewNode(FOUROPERATIONS, DIV, children); 
            }
#line 757 "parser.cpp"
    break;

  case 10: // zenvar: VARNAME
#line 133 "parser.y"
                { yylhs.value.as < std::shared_ptr<ZfxASTNode> > () = driver.makeZenVarNode(yystack_[0].value.as < string > ()); }
#line 763 "parser.cpp"
    break;

  case 11: // zenvar: DOLLAR
#line 134 "parser.y"
             { yylhs.value.as < std::shared_ptr<ZfxASTNode> > () = driver.makeZenVarNode(""); }
#line 769 "parser.cpp"
    break;

  case 12: // funcargs: exp
#line 136 "parser.y"
                         { yylhs.value.as < std::vector<std::shared_ptr<ZfxASTNode>> > () = std::vector<std::shared_ptr<ZfxASTNode>>({yystack_[0].value.as < std::shared_ptr<ZfxASTNode> > ()}); }
#line 775 "parser.cpp"
    break;

  case 13: // funcargs: funcargs COMMA exp
#line 137 "parser.y"
                         { yystack_[2].value.as < std::vector<std::shared_ptr<ZfxASTNode>> > ().push_back(yystack_[0].value.as < std::shared_ptr<ZfxASTNode> > ()); yylhs.value.as < std::vector<std::shared_ptr<ZfxASTNode>> > () = yystack_[2].value.as < std::vector<std::shared_ptr<ZfxASTNode>> > (); }
#line 781 "parser.cpp"
    break;

  case 14: // funccontent: LPAREN funcargs RPAREN
#line 139 "parser.y"
                                    { 
        yylhs.value.as < std::shared_ptr<ZfxASTNode> > () = driver.makeNewNode(FUNC, DEFAULT_FUNCVAL, yystack_[1].value.as < std::vector<std::shared_ptr<ZfxASTNode>> > ());
        yylhs.value.as < std::shared_ptr<ZfxASTNode> > ()->isParenthesisNodeComplete = true;
        yylhs.value.as < std::shared_ptr<ZfxASTNode> > ()->func_match = Match_Exactly;
    }
#line 791 "parser.cpp"
    break;

  case 15: // funccontent: LPAREN funcargs
#line 144 "parser.y"
                      { 
        yylhs.value.as < std::shared_ptr<ZfxASTNode> > () = driver.makeNewNode(FUNC, DEFAULT_FUNCVAL, yystack_[0].value.as < std::vector<std::shared_ptr<ZfxASTNode>> > ());
        yylhs.value.as < std::shared_ptr<ZfxASTNode> > ()->isParenthesisNodeComplete = false;
        yylhs.value.as < std::shared_ptr<ZfxASTNode> > ()->func_match = Match_LeftPAREN;
    }
#line 801 "parser.cpp"
    break;

  case 16: // funccontent: %empty
#line 149 "parser.y"
             {
        yylhs.value.as < std::shared_ptr<ZfxASTNode> > () = driver.makeNewNode(FUNC, DEFAULT_FUNCVAL, {});
        yylhs.value.as < std::shared_ptr<ZfxASTNode> > ()->func_match = Match_Nothing;
    }
#line 810 "parser.cpp"
    break;

  case 17: // parencontent: LPAREN exp RPAREN
#line 154 "parser.y"
                                { yylhs.value.as < std::shared_ptr<ZfxASTNode> > () = yystack_[1].value.as < std::shared_ptr<ZfxASTNode> > (); yylhs.value.as < std::shared_ptr<ZfxASTNode> > ()->paren_match = Match_Exactly; }
#line 816 "parser.cpp"
    break;

  case 18: // term: NUMBER
#line 160 "parser.y"
                        { yylhs.value.as < std::shared_ptr<ZfxASTNode> > () = driver.makeNewNumberNode(yystack_[0].value.as < float > ()); }
#line 822 "parser.cpp"
    break;

  case 19: // term: LITERAL
#line 161 "parser.y"
                        { yylhs.value.as < std::shared_ptr<ZfxASTNode> > () = driver.makeStringNode(yystack_[0].value.as < string > ()); }
#line 828 "parser.cpp"
    break;

  case 20: // term: UNCOMPSTR
#line 162 "parser.y"
                        { yylhs.value.as < std::shared_ptr<ZfxASTNode> > () = driver.makeQuoteStringNode(yystack_[0].value.as < string > ()); }
#line 834 "parser.cpp"
    break;

  case 21: // term: parencontent
#line 163 "parser.y"
                        { yylhs.value.as < std::shared_ptr<ZfxASTNode> > () = yystack_[0].value.as < std::shared_ptr<ZfxASTNode> > (); }
#line 840 "parser.cpp"
    break;

  case 22: // term: SUB exp
#line 164 "parser.y"
                        { yystack_[0].value.as < std::shared_ptr<ZfxASTNode> > ()->value = -1 * std::get<float>(yystack_[0].value.as < std::shared_ptr<ZfxASTNode> > ()->value); yylhs.value.as < std::shared_ptr<ZfxASTNode> > () = yystack_[0].value.as < std::shared_ptr<ZfxASTNode> > (); }
#line 846 "parser.cpp"
    break;

  case 23: // term: zenvar
#line 165 "parser.y"
                        { yylhs.value.as < std::shared_ptr<ZfxASTNode> > () = yystack_[0].value.as < std::shared_ptr<ZfxASTNode> > (); }
#line 852 "parser.cpp"
    break;

  case 24: // term: FUNC funccontent
#line 166 "parser.y"
                        { 
        yylhs.value.as < std::shared_ptr<ZfxASTNode> > () = yystack_[0].value.as < std::shared_ptr<ZfxASTNode> > ();
        yylhs.value.as < std::shared_ptr<ZfxASTNode> > ()->opVal = DEFAULT_FUNCVAL;
        yylhs.value.as < std::shared_ptr<ZfxASTNode> > ()->type = FUNC;
        yylhs.value.as < std::shared_ptr<ZfxASTNode> > ()->value = yystack_[1].value.as < string > ();
        yylhs.value.as < std::shared_ptr<ZfxASTNode> > ()->isParenthesisNode = true;
    }
#line 864 "parser.cpp"
    break;

  case 25: // term: %empty
#line 173 "parser.y"
             { /*$$ = driver.makeEmptyNode();*/ }
#line 870 "parser.cpp"
    break;


#line 874 "parser.cpp"

            default:
              break;
            }
        }
#if YY_EXCEPTIONS
      catch (const syntax_error& yyexc)
        {
          YYCDEBUG << "Caught exception: " << yyexc.what() << '\n';
          error (yyexc);
          YYERROR;
        }
#endif // YY_EXCEPTIONS
      YY_SYMBOL_PRINT ("-> $$ =", yylhs);
      yypop_ (yylen);
      yylen = 0;

      // Shift the result of the reduction.
      yypush_ (YY_NULLPTR, YY_MOVE (yylhs));
    }
    goto yynewstate;


  /*--------------------------------------.
  | yyerrlab -- here on detecting error.  |
  `--------------------------------------*/
  yyerrlab:
    // If not already recovering from an error, report this error.
    if (!yyerrstatus_)
      {
        ++yynerrs_;
        context yyctx (*this, yyla);
        std::string msg = yysyntax_error_ (yyctx);
        error (yyla.location, YY_MOVE (msg));
      }


    yyerror_range[1].location = yyla.location;
    if (yyerrstatus_ == 3)
      {
        /* If just tried and failed to reuse lookahead token after an
           error, discard it.  */

        // Return failure if at end of input.
        if (yyla.kind () == symbol_kind::S_YYEOF)
          YYABORT;
        else if (!yyla.empty ())
          {
            yy_destroy_ ("Error: discarding", yyla);
            yyla.clear ();
          }
      }

    // Else will try to reuse lookahead token after shifting the error token.
    goto yyerrlab1;


  /*---------------------------------------------------.
  | yyerrorlab -- error raised explicitly by YYERROR.  |
  `---------------------------------------------------*/
  yyerrorlab:
    /* Pacify compilers when the user code never invokes YYERROR and
       the label yyerrorlab therefore never appears in user code.  */
    if (false)
      YYERROR;

    /* Do not reclaim the symbols of the rule whose action triggered
       this YYERROR.  */
    yypop_ (yylen);
    yylen = 0;
    YY_STACK_PRINT ();
    goto yyerrlab1;


  /*-------------------------------------------------------------.
  | yyerrlab1 -- common code for both syntax error and YYERROR.  |
  `-------------------------------------------------------------*/
  yyerrlab1:
    yyerrstatus_ = 3;   // Each real token shifted decrements this.
    // Pop stack until we find a state that shifts the error token.
    for (;;)
      {
        yyn = yypact_[+yystack_[0].state];
        if (!yy_pact_value_is_default_ (yyn))
          {
            yyn += symbol_kind::S_YYerror;
            if (0 <= yyn && yyn <= yylast_
                && yycheck_[yyn] == symbol_kind::S_YYerror)
              {
                yyn = yytable_[yyn];
                if (0 < yyn)
                  break;
              }
          }

        // Pop the current state because it cannot handle the error token.
        if (yystack_.size () == 1)
          YYABORT;

        yyerror_range[1].location = yystack_[0].location;
        yy_destroy_ ("Error: popping", yystack_[0]);
        yypop_ ();
        YY_STACK_PRINT ();
      }
    {
      stack_symbol_type error_token;

      yyerror_range[2].location = yyla.location;
      YYLLOC_DEFAULT (error_token.location, yyerror_range, 2);

      // Shift the error token.
      error_token.state = state_type (yyn);
      yypush_ ("Shifting", YY_MOVE (error_token));
    }
    goto yynewstate;


  /*-------------------------------------.
  | yyacceptlab -- YYACCEPT comes here.  |
  `-------------------------------------*/
  yyacceptlab:
    yyresult = 0;
    goto yyreturn;


  /*-----------------------------------.
  | yyabortlab -- YYABORT comes here.  |
  `-----------------------------------*/
  yyabortlab:
    yyresult = 1;
    goto yyreturn;


  /*-----------------------------------------------------.
  | yyreturn -- parsing is finished, return the result.  |
  `-----------------------------------------------------*/
  yyreturn:
    if (!yyla.empty ())
      yy_destroy_ ("Cleanup: discarding lookahead", yyla);

    /* Do not reclaim the symbols of the rule whose action triggered
       this YYABORT or YYACCEPT.  */
    yypop_ (yylen);
    YY_STACK_PRINT ();
    while (1 < yystack_.size ())
      {
        yy_destroy_ ("Cleanup: popping", yystack_[0]);
        yypop_ ();
      }

    return yyresult;
  }
#if YY_EXCEPTIONS
    catch (...)
      {
        YYCDEBUG << "Exception caught: cleaning lookahead and stack\n";
        // Do not try to display the values of the reclaimed symbols,
        // as their printers might throw an exception.
        if (!yyla.empty ())
          yy_destroy_ (YY_NULLPTR, yyla);

        while (1 < yystack_.size ())
          {
            yy_destroy_ (YY_NULLPTR, yystack_[0]);
            yypop_ ();
          }
        throw;
      }
#endif // YY_EXCEPTIONS
  }

  void
   Parser ::error (const syntax_error& yyexc)
  {
    error (yyexc.location, yyexc.what ());
  }

  /* Return YYSTR after stripping away unnecessary quotes and
     backslashes, so that it's suitable for yyerror.  The heuristic is
     that double-quoting is unnecessary unless the string contains an
     apostrophe, a comma, or backslash (other than backslash-backslash).
     YYSTR is taken from yytname.  */
  std::string
   Parser ::yytnamerr_ (const char *yystr)
  {
    if (*yystr == '"')
      {
        std::string yyr;
        char const *yyp = yystr;

        for (;;)
          switch (*++yyp)
            {
            case '\'':
            case ',':
              goto do_not_strip_quotes;

            case '\\':
              if (*++yyp != '\\')
                goto do_not_strip_quotes;
              else
                goto append;

            append:
            default:
              yyr += *yyp;
              break;

            case '"':
              return yyr;
            }
      do_not_strip_quotes: ;
      }

    return yystr;
  }

  std::string
   Parser ::symbol_name (symbol_kind_type yysymbol)
  {
    return yytnamerr_ (yytname_[yysymbol]);
  }



  //  Parser ::context.
   Parser ::context::context (const  Parser & yyparser, const symbol_type& yyla)
    : yyparser_ (yyparser)
    , yyla_ (yyla)
  {}

  int
   Parser ::context::expected_tokens (symbol_kind_type yyarg[], int yyargn) const
  {
    // Actual number of expected tokens
    int yycount = 0;

    const int yyn = yypact_[+yyparser_.yystack_[0].state];
    if (!yy_pact_value_is_default_ (yyn))
      {
        /* Start YYX at -YYN if negative to avoid negative indexes in
           YYCHECK.  In other words, skip the first -YYN actions for
           this state because they are default actions.  */
        const int yyxbegin = yyn < 0 ? -yyn : 0;
        // Stay within bounds of both yycheck and yytname.
        const int yychecklim = yylast_ - yyn + 1;
        const int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
        for (int yyx = yyxbegin; yyx < yyxend; ++yyx)
          if (yycheck_[yyx + yyn] == yyx && yyx != symbol_kind::S_YYerror
              && !yy_table_value_is_error_ (yytable_[yyx + yyn]))
            {
              if (!yyarg)
                ++yycount;
              else if (yycount == yyargn)
                return 0;
              else
                yyarg[yycount++] = YY_CAST (symbol_kind_type, yyx);
            }
      }

    if (yyarg && yycount == 0 && 0 < yyargn)
      yyarg[0] = symbol_kind::S_YYEMPTY;
    return yycount;
  }






  int
   Parser ::yy_syntax_error_arguments_ (const context& yyctx,
                                                 symbol_kind_type yyarg[], int yyargn) const
  {
    /* There are many possibilities here to consider:
       - If this state is a consistent state with a default action, then
         the only way this function was invoked is if the default action
         is an error action.  In that case, don't check for expected
         tokens because there are none.
       - The only way there can be no lookahead present (in yyla) is
         if this state is a consistent state with a default action.
         Thus, detecting the absence of a lookahead is sufficient to
         determine that there is no unexpected or expected token to
         report.  In that case, just report a simple "syntax error".
       - Don't assume there isn't a lookahead just because this state is
         a consistent state with a default action.  There might have
         been a previous inconsistent state, consistent state with a
         non-default action, or user semantic action that manipulated
         yyla.  (However, yyla is currently not documented for users.)
       - Of course, the expected token list depends on states to have
         correct lookahead information, and it depends on the parser not
         to perform extra reductions after fetching a lookahead from the
         scanner and before detecting a syntax error.  Thus, state merging
         (from LALR or IELR) and default reductions corrupt the expected
         token list.  However, the list is correct for canonical LR with
         one exception: it will still contain any token that will not be
         accepted due to an error action in a later state.
    */

    if (!yyctx.lookahead ().empty ())
      {
        if (yyarg)
          yyarg[0] = yyctx.token ();
        int yyn = yyctx.expected_tokens (yyarg ? yyarg + 1 : yyarg, yyargn - 1);
        return yyn + 1;
      }
    return 0;
  }

  // Generate an error message.
  std::string
   Parser ::yysyntax_error_ (const context& yyctx) const
  {
    // Its maximum.
    enum { YYARGS_MAX = 5 };
    // Arguments of yyformat.
    symbol_kind_type yyarg[YYARGS_MAX];
    int yycount = yy_syntax_error_arguments_ (yyctx, yyarg, YYARGS_MAX);

    char const* yyformat = YY_NULLPTR;
    switch (yycount)
      {
#define YYCASE_(N, S)                         \
        case N:                               \
          yyformat = S;                       \
        break
      default: // Avoid compiler warnings.
        YYCASE_ (0, YY_("syntax error"));
        YYCASE_ (1, YY_("syntax error, unexpected %s"));
        YYCASE_ (2, YY_("syntax error, unexpected %s, expecting %s"));
        YYCASE_ (3, YY_("syntax error, unexpected %s, expecting %s or %s"));
        YYCASE_ (4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
        YYCASE_ (5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
#undef YYCASE_
      }

    std::string yyres;
    // Argument number.
    std::ptrdiff_t yyi = 0;
    for (char const* yyp = yyformat; *yyp; ++yyp)
      if (yyp[0] == '%' && yyp[1] == 's' && yyi < yycount)
        {
          yyres += symbol_name (yyarg[yyi++]);
          ++yyp;
        }
      else
        yyres += *yyp;
    return yyres;
  }


  const signed char  Parser ::yypact_ninf_ = -16;

  const signed char  Parser ::yytable_ninf_ = -1;

  const signed char
   Parser ::yypact_[] =
  {
     -16,     0,   -16,   -16,   -16,    -9,   -16,   -16,   -16,    16,
      16,     4,    13,   -16,   -16,   -16,    16,   -16,   -16,     1,
     -16,    16,    16,    16,    16,    20,    -2,   -16,    13,    13,
     -16,   -16,   -16,    16,    20
  };

  const signed char
   Parser ::yydefact_[] =
  {
       2,    25,     1,    18,    19,    16,    20,    11,    10,    25,
      25,     0,     4,    23,    21,     7,    25,    24,    22,     0,
       3,    25,    25,    25,    25,    12,    15,    17,     5,     6,
       8,     9,    14,    25,    13
  };

  const signed char
   Parser ::yypgoto_[] =
  {
     -16,   -16,    -7,   -15,   -16,   -16,   -16,   -16,    19
  };

  const signed char
   Parser ::yydefgoto_[] =
  {
       0,     1,    11,    12,    13,    26,    17,    14,    15
  };

  const signed char
   Parser ::yytable_[] =
  {
       2,    32,    18,    19,    27,     3,    28,    29,    33,    25,
      20,     4,     5,     6,     7,     8,    16,    21,     9,    22,
      21,     3,    22,     0,     0,    10,    34,     4,     5,     6,
       7,     8,     0,    23,     9,    24,    21,     0,    22,     0,
       0,    10,    30,    31
  };

  const signed char
   Parser ::yycheck_[] =
  {
       0,     3,     9,    10,     3,     5,    21,    22,    10,    16,
       6,    11,    12,    13,    14,    15,    25,    16,    18,    18,
      16,     5,    18,    -1,    -1,    25,    33,    11,    12,    13,
      14,    15,    -1,    20,    18,    22,    16,    -1,    18,    -1,
      -1,    25,    23,    24
  };

  const signed char
   Parser ::yystos_[] =
  {
       0,    27,     0,     5,    11,    12,    13,    14,    15,    18,
      25,    28,    29,    30,    33,    34,    25,    32,    28,    28,
       6,    16,    18,    20,    22,    28,    31,     3,    29,    29,
      34,    34,     3,    10,    28
  };

  const signed char
   Parser ::yyr1_[] =
  {
       0,    26,    27,    27,    28,    28,    28,    29,    29,    29,
      30,    30,    31,    31,    32,    32,    32,    33,    34,    34,
      34,    34,    34,    34,    34,    34
  };

  const signed char
   Parser ::yyr2_[] =
  {
       0,     2,     0,     3,     1,     3,     3,     1,     3,     3,
       1,     1,     1,     3,     3,     2,     0,     3,     1,     1,
       1,     1,     2,     1,     2,     0
  };


#if YYDEBUG || 1
  // YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
  // First, the terminals, then, starting at \a YYNTOKENS, nonterminals.
  const char*
  const  Parser ::yytname_[] =
  {
  "END", "error", "\"invalid token\"", "RPAREN", "IDENTIFIER", "NUMBER",
  "EOL", "FRAME", "FPS", "PI", "COMMA", "LITERAL", "FUNC", "UNCOMPSTR",
  "DOLLAR", "VARNAME", "ADD", "\"+\"", "SUB", "\"-\"", "MUL", "\"*\"",
  "DIV", "\"/\"", "NEG", "LPAREN", "$accept", "calclist", "exp", "factor",
  "zenvar", "funcargs", "funccontent", "parencontent", "term", YY_NULLPTR
  };
#endif


#if YYDEBUG
  const unsigned char
   Parser ::yyrline_[] =
  {
       0,   106,   106,   106,   111,   112,   116,   122,   123,   127,
     133,   134,   136,   137,   139,   144,   149,   154,   160,   161,
     162,   163,   164,   165,   166,   173
  };

  void
   Parser ::yy_stack_print_ () const
  {
    *yycdebug_ << "Stack now";
    for (stack_type::const_iterator
           i = yystack_.begin (),
           i_end = yystack_.end ();
         i != i_end; ++i)
      *yycdebug_ << ' ' << int (i->state);
    *yycdebug_ << '\n';
  }

  void
   Parser ::yy_reduce_print_ (int yyrule) const
  {
    int yylno = yyrline_[yyrule];
    int yynrhs = yyr2_[yyrule];
    // Print the symbols being reduced, and their result.
    *yycdebug_ << "Reducing stack by rule " << yyrule - 1
               << " (line " << yylno << "):\n";
    // The symbols being reduced.
    for (int yyi = 0; yyi < yynrhs; yyi++)
      YY_SYMBOL_PRINT ("   $" << yyi + 1 << " =",
                       yystack_[(yynrhs) - (yyi + 1)]);
  }
#endif // YYDEBUG


#line 10 "parser.y"
} //  zeno 
#line 1360 "parser.cpp"

#line 175 "parser.y"


// Bison expects us to provide implementation - otherwise linker complains
void zeno::Parser::error(const location &loc , const std::string &message) {
        
        // Location should be initialized inside scanner action, but is not in this example.
        // Let's grab location directly from driver class.
	// cout << "Error: " << message << endl << "Location: " << loc << endl;

    cout << "Error: " << message << endl << "Error location: " << driver.location() << endl;
}

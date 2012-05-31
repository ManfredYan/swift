//===--- Parser.h - Swift Language Parser -----------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Parser interface.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_PARSER_H
#define SWIFT_PARSER_H

#include "Scope.h"
#include "swift/Parse/Token.h"
#include "swift/AST/AST.h"
#include "swift/AST/Diagnostics.h"
#include "llvm/ADT/SetVector.h"

namespace llvm {
  class Component;
  class MemoryBuffer;
  class SourceMgr;
  template <typename PT1, typename PT2, typename PT3> class PointerUnion3;
}

namespace swift {
  class DiagnosticEngine;
  class Lexer;
  class ScopeInfo;
  class TupleType;
  
  struct OneOfElementInfo;

class Parser {
  Parser(const Parser&) = delete;
  void operator=(const Parser&) = delete;
  
  Identifier GetIdent;
  Identifier SetIdent;
  
public:
  llvm::SourceMgr &SourceMgr;
  DiagnosticEngine &Diags;
  const llvm::MemoryBuffer *Buffer;
  Lexer *L;
  DeclContext *CurDeclContext;
  swift::Component *Component;
  ASTContext &Context;
  ScopeInfo ScopeInfo;
  std::vector<TranslationUnit::IdentTypeAndContext> UnresolvedIdentifierTypes;
  std::vector<llvm::SetVector<ValueDecl*>> ValCaptures;
  std::vector<std::vector<VarDecl*>> AnonClosureVars;
  std::vector<TranslationUnit::TupleTypeAndContext> TypesWithDefaultValues;
  bool IsMainModule;
  bool FoundSideEffects;

  /// Tok - This is the current token being considered by the parser.
  Token Tok;
  
  typedef llvm::PointerUnion3<Expr*, Stmt*, Decl*> ExprStmtOrDecl;

  /// A RAII object for temporarily changing CurDeclContext.
  class ContextChange {
    Parser &P;
    DeclContext *OldContext;
  public:
    ContextChange(Parser &P, DeclContext *DC)
      : P(P), OldContext(P.CurDeclContext) {
      assert(DC && "pushing null context?");
      P.CurDeclContext = DC;
      P.ValCaptures.emplace_back();
    }

    /// Prematurely pop the DeclContext installed by the constructor.
    /// Makes the destructor a no-op.
    void pop() {
      assert(OldContext && "already popped context!");
      P.CurDeclContext = OldContext;
      OldContext = nullptr;
      P.ValCaptures.pop_back();
    }

    ~ContextChange() {
      if (OldContext) P.CurDeclContext = OldContext;
    }
  };


  Parser(unsigned BufferID, swift::Component *Component, ASTContext &Ctx,
         unsigned Offset, unsigned EndOffset, bool IsMainModule);
  ~Parser();
  
  //===--------------------------------------------------------------------===//
  // Utilities
  
  /// peekToken - Return the next token that will be installed by consumeToken.
  const Token &peekToken();
  
  // Utilities.
  SourceLoc consumeToken();
  SourceLoc consumeToken(tok K) {
    assert(Tok.is(K) && "Consuming wrong token kind");
    return consumeToken();
  }
  
  /// consumeIf - If the current token is the specified kind, consume it and
  /// return true.  Otherwise, return false without consuming it.
  bool consumeIf(tok K) {
    if (Tok.isNot(K)) return false;
    consumeToken(K);
    return true;
  }
  
  /// skipUntil - Read tokens until we get to one of the specified tokens, then
  /// return without consuming it.  Because we cannot guarantee that the token 
  /// will ever occur, this skips to some likely good stopping point.
  ///
  void skipUntil(tok T1, tok T2 = tok::unknown);
  
  /// skipUntilDeclStmtRBrace - Skip to the next decl or '}'.
  void skipUntilDeclRBrace();

  /// skipUntilDeclStmtRBrace - Skip to the next decl, statement or '}'.
  void skipUntilDeclStmtRBrace();
  
  template<typename ...ArgTypes>
  InFlightDiagnostic diagnose(SourceLoc Loc, ArgTypes... Args) {
    return Diags.diagnose(Loc, Diagnostic(Args...));
  }
  template<typename ...ArgTypes>
  InFlightDiagnostic diagnose(Token Tok, ArgTypes... Args) {
    return Diags.diagnose(Tok.getLoc(), Diagnostic(Args...));
  }
     
  /// \brief Check whether the current token starts with '<'.
  bool startsWithLess(Token Tok) {
    return Tok.is(tok::oper) && Tok.getText()[0] == '<';
  }

  /// \brief Check whether the current token starts with '>'.
  bool startsWithGreater(Token Tok) {
    return Tok.is(tok::oper) && Tok.getText()[0] == '>';
  }

  /// \brief Consume the starting '<' of the current token, which may either
  /// be a complete '<' token or some kind of operator token starting with '<',
  /// e.g., '<>'.
  SourceLoc consumeStartingLess();

  /// \brief Consume the starting '>' of the current token, which may either
  /// be a complete '>' token or some kind of operator token starting with '>',
  /// e.g., '>>'.
  SourceLoc consumeStartingGreater();

  //===--------------------------------------------------------------------===//
  // Primitive Parsing

  /// parseIdentifier - Consume an identifier (but not an operator) if
  /// present and return its name in Result.  Otherwise, emit an error and
  /// return true.
  bool parseIdentifier(Identifier &Result, const Diagnostic &D);
  
  template<typename ...ArgTypes>
  bool parseIdentifier(Identifier &Result,  ArgTypes... Args) {
    return parseIdentifier(Result, Diagnostic(Args...));
  }
  
  /// parseAnyIdentifier - Consume an identifier or operator if present and
  /// return its name in Result.  Otherwise, emit an error and return true.
  bool parseAnyIdentifier(Identifier &Result, const Diagnostic &D);

  template<typename ...ArgTypes>
  bool parseAnyIdentifier(Identifier &Result,  ArgTypes... Args) {
    return parseAnyIdentifier(Result, Diagnostic(Args...));
  }

  /// parseToken - The parser expects that 'K' is next in the input.  If so, it
  /// is consumed and false is returned.
  ///
  /// If the input is malformed, this emits the specified error diagnostic.
  /// Next, if SkipToTok is specified, it calls skipUntil(SkipToTok).  Finally,
  /// true is returned.
  bool parseToken(tok K, Diag<> D, tok SkipToTok = tok::unknown) {
    SourceLoc L;
    return parseToken(K, L, D, SkipToTok);
  }
  bool parseToken(tok K, SourceLoc &TokLoc, Diag<> D,
                  tok SkipToTok = tok::unknown);
  
  /// parseMatchingToken - Parse the specified expected token and return its
  /// location on success.  On failure, emit the specified error diagnostic, and
  /// a note at the specified note location.
  bool parseMatchingToken(tok K, SourceLoc &TokLoc, Diag<> ErrorDiag,
                          SourceLoc OtherLoc, Diag<> OtherNote,
                          tok SkipToTok = tok::unknown);
  
  bool parseValueSpecifier(Type &Ty, NullablePtr<Expr> &Init);

  void parseBraceItemList(SmallVectorImpl<ExprStmtOrDecl> &Decls,
                          bool IsTopLevel);

  //===--------------------------------------------------------------------===//
  // Decl Parsing
  static bool isStartOfDecl(const Token &Tok, const Token &Tok2);

  void parseTranslationUnit(TranslationUnit *TU);
  bool parseDecl(SmallVectorImpl<Decl*> &Entries, unsigned Flags);
  enum {
    PD_Default              = 0,
    PD_AllowTopLevel        = 1 << 1,
    PD_DisallowVar          = 1 << 2,
    PD_DisallowOperators    = 1 << 3,
    PD_HasContainerType     = 1 << 4,
    PD_DisallowProperty     = 1 << 5,
    PD_DisallowNominalTypes = 1 << 6,
    PD_DisallowStatic       = 1 << 7,
    PD_DisallowFuncDef      = 1 << 8,
    PD_DisallowInit         = 1 << 9,
    PD_DisallowTypeAliasDef = 1 << 10 
  };
  
  TypeAliasDecl *parseDeclTypeAlias(bool WantDefinition);
  /// addVarsToScope - Add the variables in the given pattern to the current
  /// scope, collecting the variables in the vector \c Decls and applying
  /// \c Attributes to each one.
  void addVarsToScope(Pattern *Pat, SmallVectorImpl<Decl*> &Decls,
                      DeclAttributes &Attributes);
  void parseAttributeList(DeclAttributes &Attributes) {
    if (Tok.isAnyLSquare())
      parseAttributeListPresent(Attributes);
  }
  void parseAttributeListPresent(DeclAttributes &Attributes);
  bool parseAttribute(DeclAttributes &Attributes);
  
  Decl *parseDeclImport();
  bool parseInheritance(SmallVectorImpl<Type> &Inherited);
  Decl *parseDeclExtension();
  Decl *parseExtensionBody(SourceLoc ExtensionLoc, Type Ty,
                           MutableArrayRef<Type> Inherited);
  bool parseDeclOneOf(SmallVectorImpl<Decl*> &Decls);

  bool parseDeclStruct(SmallVectorImpl<Decl*> &Decls);
  bool parseDeclClass(SmallVectorImpl<Decl*> &Decls);
  bool parseDeclVar(bool hasContainerType, SmallVectorImpl<Decl*> &Decls);
  bool parseGetSet(bool HasContainerType, Pattern *Indices, Type ElementTy, 
                   FuncDecl *&Get, FuncDecl *&Set, SourceLoc &LastValidLoc);
  void parseDeclVarGetSet(Pattern &pattern, bool hasContainerType);
  
  Pattern *buildImplicitThisParameter();
  FuncDecl *parseDeclFunc(bool hasContainerType = false);
  Decl *parseDeclProtocol();
  bool parseProtocolBody(SourceLoc ProtocolLoc, const DeclAttributes &Attrs,
                         ProtocolDecl *Proto);
  
  bool parseDeclSubscript(bool HasContainerType,
                          bool NeedDefinition,
                          SmallVectorImpl<Decl *> &Decls);
  
  //===--------------------------------------------------------------------===//
  // Type Parsing
  
  bool parseType(Type &Result);
  bool parseType(Type &Result, Diag<> ID);
  bool parseTypeAnnotation(Type &Result);
  bool parseTypeAnnotation(Type &Result, Diag<> ID);
  bool parseTypeIdentifier(Type &Result);
  bool parseTypeComposition(Type &Result);
  bool parseTypeTupleBody(SourceLoc LPLoc, Type &Result);
  
  bool parseTypeArray(Type &result);

  //===--------------------------------------------------------------------===//
  // Pattern Parsing

  bool parseFunctionSignature(SmallVectorImpl<Pattern*> &params, Type &type);
  bool buildFunctionSignature(SmallVectorImpl<Pattern*> &params, Type &type);
  NullablePtr<Pattern> parsePattern();
  NullablePtr<Pattern> parsePatternTuple();
  NullablePtr<Pattern> parsePatternAtom();
  bool checkFullyTyped(Type Ty);
  bool checkFullyTyped(Pattern *Param, Type &funcTy);
  
  //===--------------------------------------------------------------------===//
  // Expression Parsing
  
  // Each of these parsing methods returns null (in a NullablePtr) on a parse
  // error, or an ErrorExpr on a semantic error.  If the method cannot fail, it
  // returns a raw Expr*.
  NullablePtr<Expr> parseExpr(Diag<> ID);
  NullablePtr<Expr> parseExprPostfix(Diag<> ID);
  NullablePtr<Expr> parseExprUnary(Diag<> ID);
  NullablePtr<Expr> parseExprNew();
  Expr *parseExprStringLiteral();
  Expr *parseExprIdentifier();
  NullablePtr<Expr> parseExprExplicitClosure();
  Expr *parseExprAnonClosureArg();
  NullablePtr<Expr> parseExprParen();
  NullablePtr<Expr> parseExprFunc();
  
  Expr *parseExprOperator();
  Expr *actOnIdentifierExpr(Identifier Text, SourceLoc Loc);
  FuncExpr *actOnFuncExprStart(SourceLoc FuncLoc, Type FuncTy,
                               ArrayRef<Pattern*> Patterns);

  //===--------------------------------------------------------------------===//
  // Statement Parsing
  // Each of these returns null (in a NullablePtr) on a parse error, or an
  // ErrorStmt on a semantic error.
  static bool isStartOfStmtOtherThanAssignment(const Token &Tok);
  NullablePtr<Stmt> parseStmtOtherThanAssignment();
  bool parseExprOrStmtAssign(ExprStmtOrDecl &Results);
  bool parseExprOrStmt(ExprStmtOrDecl &Results);
  NullablePtr<BraceStmt> parseStmtBrace(Diag<> ID);
  NullablePtr<Stmt> parseStmtReturn();
  NullablePtr<Stmt> parseStmtIf();
  NullablePtr<Stmt> parseStmtWhile();
  NullablePtr<Stmt> parseStmtFor();
  NullablePtr<Stmt> parseStmtForCStyle(SourceLoc ForLoc);
  NullablePtr<Stmt> parseStmtForEach(SourceLoc ForLoc);
};

} // end namespace swift

#endif

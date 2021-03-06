//===-- LLVMContextImpl.h - The LLVMContextImpl opaque class ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file declares LLVMContextImpl, the opaque implementation 
//  of LLVMContext.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_IR_LLVMCONTEXTIMPL_H
#define LLVM_LIB_IR_LLVMCONTEXTIMPL_H

#include "AttributeImpl.h"
#include "ConstantsContext.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/ValueHandle.h"
#include <vector>

#include <map>

namespace llvm {

class ConstantInt;
class ConstantFP;
class DiagnosticInfoOptimizationRemark;
class DiagnosticInfoOptimizationRemarkMissed;
class DiagnosticInfoOptimizationRemarkAnalysis;
class GCStrategy;
class LLVMContext;
class Type;
class Value;

struct DenseMapAPIntKeyInfo {
  static inline APInt getEmptyKey() {
    APInt V(nullptr, 0);
    V.VAL = 0;
    return V;
  }
  static inline APInt getTombstoneKey() {
    APInt V(nullptr, 0);
    V.VAL = 1;
    return V;
  }
  static unsigned getHashValue(const APInt &Key) {
    return static_cast<unsigned>(hash_value(Key));
  }
  static bool isEqual(const APInt &LHS, const APInt &RHS) {
    return LHS.getBitWidth() == RHS.getBitWidth() && LHS == RHS;
  }
};

struct DenseMapAPFloatKeyInfo {
  static inline APFloat getEmptyKey() { return APFloat(APFloat::Bogus, 1); }
  static inline APFloat getTombstoneKey() { return APFloat(APFloat::Bogus, 2); }
  static unsigned getHashValue(const APFloat &Key) {
    return static_cast<unsigned>(hash_value(Key));
  }
  static bool isEqual(const APFloat &LHS, const APFloat &RHS) {
    return LHS.bitwiseIsEqual(RHS);
  }
};

struct AnonStructTypeKeyInfo {
  struct KeyTy {
    ArrayRef<Type*> ETypes;
    bool isPacked;
    KeyTy(const ArrayRef<Type*>& E, bool P) :
      ETypes(E), isPacked(P) {}
    KeyTy(const StructType *ST)
        : ETypes(ST->elements()), isPacked(ST->isPacked()) {}
    bool operator==(const KeyTy& that) const {
      if (isPacked != that.isPacked)
        return false;
      if (ETypes != that.ETypes)
        return false;
      return true;
    }
    bool operator!=(const KeyTy& that) const {
      return !this->operator==(that);
    }
  };
  static inline StructType* getEmptyKey() {
    return DenseMapInfo<StructType*>::getEmptyKey();
  }
  static inline StructType* getTombstoneKey() {
    return DenseMapInfo<StructType*>::getTombstoneKey();
  }
  static unsigned getHashValue(const KeyTy& Key) {
    return hash_combine(hash_combine_range(Key.ETypes.begin(),
                                           Key.ETypes.end()),
                        Key.isPacked);
  }
  static unsigned getHashValue(const StructType *ST) {
    return getHashValue(KeyTy(ST));
  }
  static bool isEqual(const KeyTy& LHS, const StructType *RHS) {
    if (RHS == getEmptyKey() || RHS == getTombstoneKey())
      return false;
    return LHS == KeyTy(RHS);
  }
  static bool isEqual(const StructType *LHS, const StructType *RHS) {
    return LHS == RHS;
  }
};

struct FunctionTypeKeyInfo {
  struct KeyTy {
    const Type *ReturnType;
    ArrayRef<Type*> Params;
    bool isVarArg;
    KeyTy(const Type* R, const ArrayRef<Type*>& P, bool V) :
      ReturnType(R), Params(P), isVarArg(V) {}
    KeyTy(const FunctionType *FT)
        : ReturnType(FT->getReturnType()), Params(FT->params()),
          isVarArg(FT->isVarArg()) {}
    bool operator==(const KeyTy& that) const {
      if (ReturnType != that.ReturnType)
        return false;
      if (isVarArg != that.isVarArg)
        return false;
      if (Params != that.Params)
        return false;
      return true;
    }
    bool operator!=(const KeyTy& that) const {
      return !this->operator==(that);
    }
  };
  static inline FunctionType* getEmptyKey() {
    return DenseMapInfo<FunctionType*>::getEmptyKey();
  }
  static inline FunctionType* getTombstoneKey() {
    return DenseMapInfo<FunctionType*>::getTombstoneKey();
  }
  static unsigned getHashValue(const KeyTy& Key) {
    return hash_combine(Key.ReturnType,
                        hash_combine_range(Key.Params.begin(),
                                           Key.Params.end()),
                        Key.isVarArg);
  }
  static unsigned getHashValue(const FunctionType *FT) {
    return getHashValue(KeyTy(FT));
  }
  static bool isEqual(const KeyTy& LHS, const FunctionType *RHS) {
    if (RHS == getEmptyKey() || RHS == getTombstoneKey())
      return false;
    return LHS == KeyTy(RHS);
  }
  static bool isEqual(const FunctionType *LHS, const FunctionType *RHS) {
    return LHS == RHS;
  }
};

/// \brief Structure for hashing arbitrary MDNode operands.
class MDNodeOpsKey {
  ArrayRef<Metadata *> RawOps;
  ArrayRef<MDOperand> Ops;

  unsigned Hash;

protected:
  MDNodeOpsKey(ArrayRef<Metadata *> Ops)
      : RawOps(Ops), Hash(calculateHash(Ops)) {}

  template <class NodeTy>
  MDNodeOpsKey(const NodeTy *N, unsigned Offset = 0)
      : Ops(N->op_begin() + Offset, N->op_end()), Hash(N->getHash()) {}

  template <class NodeTy>
  bool compareOps(const NodeTy *RHS, unsigned Offset = 0) const {
    if (getHash() != RHS->getHash())
      return false;

    assert((RawOps.empty() || Ops.empty()) && "Two sets of operands?");
    return RawOps.empty() ? compareOps(Ops, RHS, Offset)
                          : compareOps(RawOps, RHS, Offset);
  }

  static unsigned calculateHash(MDNode *N, unsigned Offset = 0);

private:
  template <class T>
  static bool compareOps(ArrayRef<T> Ops, const MDNode *RHS, unsigned Offset) {
    if (Ops.size() != RHS->getNumOperands() - Offset)
      return false;
    return std::equal(Ops.begin(), Ops.end(), RHS->op_begin() + Offset);
  }

  static unsigned calculateHash(ArrayRef<Metadata *> Ops);

public:
  unsigned getHash() const { return Hash; }
};

template <class NodeTy> struct MDNodeKeyImpl;
template <class NodeTy> struct MDNodeInfo;

/// \brief DenseMapInfo for MDTuple.
///
/// Note that we don't need the is-function-local bit, since that's implicit in
/// the operands.
template <> struct MDNodeKeyImpl<MDTuple> : MDNodeOpsKey {
  MDNodeKeyImpl(ArrayRef<Metadata *> Ops) : MDNodeOpsKey(Ops) {}
  MDNodeKeyImpl(const MDTuple *N) : MDNodeOpsKey(N) {}

  bool isKeyOf(const MDTuple *RHS) const { return compareOps(RHS); }

  unsigned getHashValue() const { return getHash(); }

  static unsigned calculateHash(MDTuple *N) {
    return MDNodeOpsKey::calculateHash(N);
  }
};

/// \brief DenseMapInfo for MDLocation.
template <> struct MDNodeKeyImpl<MDLocation> {
  unsigned Line;
  unsigned Column;
  Metadata *Scope;
  Metadata *InlinedAt;

  MDNodeKeyImpl(unsigned Line, unsigned Column, Metadata *Scope,
                Metadata *InlinedAt)
      : Line(Line), Column(Column), Scope(Scope), InlinedAt(InlinedAt) {}

  MDNodeKeyImpl(const MDLocation *L)
      : Line(L->getLine()), Column(L->getColumn()), Scope(L->getRawScope()),
        InlinedAt(L->getRawInlinedAt()) {}

  bool isKeyOf(const MDLocation *RHS) const {
    return Line == RHS->getLine() && Column == RHS->getColumn() &&
           Scope == RHS->getRawScope() && InlinedAt == RHS->getRawInlinedAt();
  }
  unsigned getHashValue() const {
    return hash_combine(Line, Column, Scope, InlinedAt);
  }
};

/// \brief DenseMapInfo for GenericDebugNode.
template <> struct MDNodeKeyImpl<GenericDebugNode> : MDNodeOpsKey {
  unsigned Tag;
  StringRef Header;
  MDNodeKeyImpl(unsigned Tag, StringRef Header, ArrayRef<Metadata *> DwarfOps)
      : MDNodeOpsKey(DwarfOps), Tag(Tag), Header(Header) {}
  MDNodeKeyImpl(const GenericDebugNode *N)
      : MDNodeOpsKey(N, 1), Tag(N->getTag()), Header(N->getHeader()) {}

  bool isKeyOf(const GenericDebugNode *RHS) const {
    return Tag == RHS->getTag() && Header == RHS->getHeader() &&
           compareOps(RHS, 1);
  }

  unsigned getHashValue() const { return hash_combine(getHash(), Tag, Header); }

  static unsigned calculateHash(GenericDebugNode *N) {
    return MDNodeOpsKey::calculateHash(N, 1);
  }
};

template <> struct MDNodeKeyImpl<MDSubrange> {
  int64_t Count;
  int64_t LowerBound;

  MDNodeKeyImpl(int64_t Count, int64_t LowerBound)
      : Count(Count), LowerBound(LowerBound) {}
  MDNodeKeyImpl(const MDSubrange *N)
      : Count(N->getCount()), LowerBound(N->getLowerBound()) {}

  bool isKeyOf(const MDSubrange *RHS) const {
    return Count == RHS->getCount() && LowerBound == RHS->getLowerBound();
  }
  unsigned getHashValue() const { return hash_combine(Count, LowerBound); }
};

template <> struct MDNodeKeyImpl<MDEnumerator> {
  int64_t Value;
  StringRef Name;

  MDNodeKeyImpl(int64_t Value, StringRef Name) : Value(Value), Name(Name) {}
  MDNodeKeyImpl(const MDEnumerator *N)
      : Value(N->getValue()), Name(N->getName()) {}

  bool isKeyOf(const MDEnumerator *RHS) const {
    return Value == RHS->getValue() && Name == RHS->getName();
  }
  unsigned getHashValue() const { return hash_combine(Value, Name); }
};

template <> struct MDNodeKeyImpl<MDBasicType> {
  unsigned Tag;
  StringRef Name;
  uint64_t SizeInBits;
  uint64_t AlignInBits;
  unsigned Encoding;

  MDNodeKeyImpl(unsigned Tag, StringRef Name, uint64_t SizeInBits,
                uint64_t AlignInBits, unsigned Encoding)
      : Tag(Tag), Name(Name), SizeInBits(SizeInBits), AlignInBits(AlignInBits),
        Encoding(Encoding) {}
  MDNodeKeyImpl(const MDBasicType *N)
      : Tag(N->getTag()), Name(N->getName()), SizeInBits(N->getSizeInBits()),
        AlignInBits(N->getAlignInBits()), Encoding(N->getEncoding()) {}

  bool isKeyOf(const MDBasicType *RHS) const {
    return Tag == RHS->getTag() && Name == RHS->getName() &&
           SizeInBits == RHS->getSizeInBits() &&
           AlignInBits == RHS->getAlignInBits() &&
           Encoding == RHS->getEncoding();
  }
  unsigned getHashValue() const {
    return hash_combine(Tag, Name, SizeInBits, AlignInBits, Encoding);
  }
};

template <> struct MDNodeKeyImpl<MDDerivedType> {
  unsigned Tag;
  StringRef Name;
  Metadata *File;
  unsigned Line;
  Metadata *Scope;
  Metadata *BaseType;
  uint64_t SizeInBits;
  uint64_t AlignInBits;
  uint64_t OffsetInBits;
  unsigned Flags;
  Metadata *ExtraData;

  MDNodeKeyImpl(unsigned Tag, StringRef Name, Metadata *File, unsigned Line,
                Metadata *Scope, Metadata *BaseType, uint64_t SizeInBits,
                uint64_t AlignInBits, uint64_t OffsetInBits, unsigned Flags,
                Metadata *ExtraData)
      : Tag(Tag), Name(Name), File(File), Line(Line), Scope(Scope),
        BaseType(BaseType), SizeInBits(SizeInBits), AlignInBits(AlignInBits),
        OffsetInBits(OffsetInBits), Flags(Flags), ExtraData(ExtraData) {}
  MDNodeKeyImpl(const MDDerivedType *N)
      : Tag(N->getTag()), Name(N->getName()), File(N->getRawFile()),
        Line(N->getLine()), Scope(N->getRawScope()),
        BaseType(N->getRawBaseType()), SizeInBits(N->getSizeInBits()),
        AlignInBits(N->getAlignInBits()), OffsetInBits(N->getOffsetInBits()),
        Flags(N->getFlags()), ExtraData(N->getRawExtraData()) {}

  bool isKeyOf(const MDDerivedType *RHS) const {
    return Tag == RHS->getTag() && Name == RHS->getName() &&
           File == RHS->getRawFile() && Line == RHS->getLine() &&
           Scope == RHS->getRawScope() && BaseType == RHS->getRawBaseType() &&
           SizeInBits == RHS->getSizeInBits() &&
           AlignInBits == RHS->getAlignInBits() &&
           OffsetInBits == RHS->getOffsetInBits() && Flags == RHS->getFlags() &&
           ExtraData == RHS->getRawExtraData();
  }
  unsigned getHashValue() const {
    return hash_combine(Tag, Name, File, Line, Scope, BaseType, SizeInBits,
                        AlignInBits, OffsetInBits, Flags, ExtraData);
  }
};

template <> struct MDNodeKeyImpl<MDCompositeType> {
  unsigned Tag;
  StringRef Name;
  Metadata *File;
  unsigned Line;
  Metadata *Scope;
  Metadata *BaseType;
  uint64_t SizeInBits;
  uint64_t AlignInBits;
  uint64_t OffsetInBits;
  unsigned Flags;
  Metadata *Elements;
  unsigned RuntimeLang;
  Metadata *VTableHolder;
  Metadata *TemplateParams;
  StringRef Identifier;

  MDNodeKeyImpl(unsigned Tag, StringRef Name, Metadata *File, unsigned Line,
                Metadata *Scope, Metadata *BaseType, uint64_t SizeInBits,
                uint64_t AlignInBits, uint64_t OffsetInBits, unsigned Flags,
                Metadata *Elements, unsigned RuntimeLang,
                Metadata *VTableHolder, Metadata *TemplateParams,
                StringRef Identifier)
      : Tag(Tag), Name(Name), File(File), Line(Line), Scope(Scope),
        BaseType(BaseType), SizeInBits(SizeInBits), AlignInBits(AlignInBits),
        OffsetInBits(OffsetInBits), Flags(Flags), Elements(Elements),
        RuntimeLang(RuntimeLang), VTableHolder(VTableHolder),
        TemplateParams(TemplateParams), Identifier(Identifier) {}
  MDNodeKeyImpl(const MDCompositeType *N)
      : Tag(N->getTag()), Name(N->getName()), File(N->getRawFile()),
        Line(N->getLine()), Scope(N->getRawScope()),
        BaseType(N->getRawBaseType()), SizeInBits(N->getSizeInBits()),
        AlignInBits(N->getAlignInBits()), OffsetInBits(N->getOffsetInBits()),
        Flags(N->getFlags()), Elements(N->getRawElements()),
        RuntimeLang(N->getRuntimeLang()), VTableHolder(N->getRawVTableHolder()),
        TemplateParams(N->getRawTemplateParams()),
        Identifier(N->getIdentifier()) {}

  bool isKeyOf(const MDCompositeType *RHS) const {
    return Tag == RHS->getTag() && Name == RHS->getName() &&
           File == RHS->getRawFile() && Line == RHS->getLine() &&
           Scope == RHS->getRawScope() && BaseType == RHS->getRawBaseType() &&
           SizeInBits == RHS->getSizeInBits() &&
           AlignInBits == RHS->getAlignInBits() &&
           OffsetInBits == RHS->getOffsetInBits() && Flags == RHS->getFlags() &&
           Elements == RHS->getRawElements() &&
           RuntimeLang == RHS->getRuntimeLang() &&
           VTableHolder == RHS->getRawVTableHolder() &&
           TemplateParams == RHS->getRawTemplateParams() &&
           Identifier == RHS->getIdentifier();
  }
  unsigned getHashValue() const {
    return hash_combine(Tag, Name, File, Line, Scope, BaseType, SizeInBits,
                        AlignInBits, OffsetInBits, Flags, Elements, RuntimeLang,
                        VTableHolder, TemplateParams, Identifier);
  }
};

template <> struct MDNodeKeyImpl<MDSubroutineType> {
  unsigned Flags;
  Metadata *TypeArray;

  MDNodeKeyImpl(int64_t Flags, Metadata *TypeArray)
      : Flags(Flags), TypeArray(TypeArray) {}
  MDNodeKeyImpl(const MDSubroutineType *N)
      : Flags(N->getFlags()), TypeArray(N->getRawTypeArray()) {}

  bool isKeyOf(const MDSubroutineType *RHS) const {
    return Flags == RHS->getFlags() && TypeArray == RHS->getRawTypeArray();
  }
  unsigned getHashValue() const { return hash_combine(Flags, TypeArray); }
};

template <> struct MDNodeKeyImpl<MDFile> {
  StringRef Filename;
  StringRef Directory;

  MDNodeKeyImpl(StringRef Filename, StringRef Directory)
      : Filename(Filename), Directory(Directory) {}
  MDNodeKeyImpl(const MDFile *N)
      : Filename(N->getFilename()), Directory(N->getDirectory()) {}

  bool isKeyOf(const MDFile *RHS) const {
    return Filename == RHS->getFilename() && Directory == RHS->getDirectory();
  }
  unsigned getHashValue() const { return hash_combine(Filename, Directory); }
};

template <> struct MDNodeKeyImpl<MDCompileUnit> {
  unsigned SourceLanguage;
  Metadata *File;
  StringRef Producer;
  bool IsOptimized;
  StringRef Flags;
  unsigned RuntimeVersion;
  StringRef SplitDebugFilename;
  unsigned EmissionKind;
  Metadata *EnumTypes;
  Metadata *RetainedTypes;
  Metadata *Subprograms;
  Metadata *GlobalVariables;
  Metadata *ImportedEntities;

  MDNodeKeyImpl(unsigned SourceLanguage, Metadata *File, StringRef Producer,
                bool IsOptimized, StringRef Flags, unsigned RuntimeVersion,
                StringRef SplitDebugFilename, unsigned EmissionKind,
                Metadata *EnumTypes, Metadata *RetainedTypes,
                Metadata *Subprograms, Metadata *GlobalVariables,
                Metadata *ImportedEntities)
      : SourceLanguage(SourceLanguage), File(File), Producer(Producer),
        IsOptimized(IsOptimized), Flags(Flags), RuntimeVersion(RuntimeVersion),
        SplitDebugFilename(SplitDebugFilename), EmissionKind(EmissionKind),
        EnumTypes(EnumTypes), RetainedTypes(RetainedTypes),
        Subprograms(Subprograms), GlobalVariables(GlobalVariables),
        ImportedEntities(ImportedEntities) {}
  MDNodeKeyImpl(const MDCompileUnit *N)
      : SourceLanguage(N->getSourceLanguage()), File(N->getRawFile()),
        Producer(N->getProducer()), IsOptimized(N->isOptimized()),
        Flags(N->getFlags()), RuntimeVersion(N->getRuntimeVersion()),
        SplitDebugFilename(N->getSplitDebugFilename()),
        EmissionKind(N->getEmissionKind()), EnumTypes(N->getRawEnumTypes()),
        RetainedTypes(N->getRawRetainedTypes()),
        Subprograms(N->getRawSubprograms()),
        GlobalVariables(N->getRawGlobalVariables()),
        ImportedEntities(N->getRawImportedEntities()) {}

  bool isKeyOf(const MDCompileUnit *RHS) const {
    return SourceLanguage == RHS->getSourceLanguage() &&
           File == RHS->getRawFile() && Producer == RHS->getProducer() &&
           IsOptimized == RHS->isOptimized() && Flags == RHS->getFlags() &&
           RuntimeVersion == RHS->getRuntimeVersion() &&
           SplitDebugFilename == RHS->getSplitDebugFilename() &&
           EmissionKind == RHS->getEmissionKind() &&
           EnumTypes == RHS->getRawEnumTypes() &&
           RetainedTypes == RHS->getRawRetainedTypes() &&
           Subprograms == RHS->getRawSubprograms() &&
           GlobalVariables == RHS->getRawGlobalVariables() &&
           ImportedEntities == RHS->getRawImportedEntities();
  }
  unsigned getHashValue() const {
    return hash_combine(SourceLanguage, File, Producer, IsOptimized, Flags,
                        RuntimeVersion, SplitDebugFilename, EmissionKind,
                        EnumTypes, RetainedTypes, Subprograms, GlobalVariables,
                        ImportedEntities);
  }
};

template <> struct MDNodeKeyImpl<MDSubprogram> {
  Metadata *Scope;
  StringRef Name;
  StringRef LinkageName;
  Metadata *File;
  unsigned Line;
  Metadata *Type;
  bool IsLocalToUnit;
  bool IsDefinition;
  unsigned ScopeLine;
  Metadata *ContainingType;
  unsigned Virtuality;
  unsigned VirtualIndex;
  unsigned Flags;
  bool IsOptimized;
  Metadata *Function;
  Metadata *TemplateParams;
  Metadata *Declaration;
  Metadata *Variables;

  MDNodeKeyImpl(Metadata *Scope, StringRef Name, StringRef LinkageName,
                Metadata *File, unsigned Line, Metadata *Type,
                bool IsLocalToUnit, bool IsDefinition, unsigned ScopeLine,
                Metadata *ContainingType, unsigned Virtuality,
                unsigned VirtualIndex, unsigned Flags, bool IsOptimized,
                Metadata *Function, Metadata *TemplateParams,
                Metadata *Declaration, Metadata *Variables)
      : Scope(Scope), Name(Name), LinkageName(LinkageName), File(File),
        Line(Line), Type(Type), IsLocalToUnit(IsLocalToUnit),
        IsDefinition(IsDefinition), ScopeLine(ScopeLine),
        ContainingType(ContainingType), Virtuality(Virtuality),
        VirtualIndex(VirtualIndex), Flags(Flags), IsOptimized(IsOptimized),
        Function(Function), TemplateParams(TemplateParams),
        Declaration(Declaration), Variables(Variables) {}
  MDNodeKeyImpl(const MDSubprogram *N)
      : Scope(N->getRawScope()), Name(N->getName()),
        LinkageName(N->getLinkageName()), File(N->getRawFile()),
        Line(N->getLine()), Type(N->getRawType()),
        IsLocalToUnit(N->isLocalToUnit()), IsDefinition(N->isDefinition()),
        ScopeLine(N->getScopeLine()), ContainingType(N->getRawContainingType()),
        Virtuality(N->getVirtuality()), VirtualIndex(N->getVirtualIndex()),
        Flags(N->getFlags()), IsOptimized(N->isOptimized()),
        Function(N->getRawFunction()),
        TemplateParams(N->getRawTemplateParams()),
        Declaration(N->getRawDeclaration()), Variables(N->getRawVariables()) {}

  bool isKeyOf(const MDSubprogram *RHS) const {
    return Scope == RHS->getRawScope() && Name == RHS->getName() &&
           LinkageName == RHS->getLinkageName() && File == RHS->getRawFile() &&
           Line == RHS->getLine() && Type == RHS->getRawType() &&
           IsLocalToUnit == RHS->isLocalToUnit() &&
           IsDefinition == RHS->isDefinition() &&
           ScopeLine == RHS->getScopeLine() &&
           ContainingType == RHS->getRawContainingType() &&
           Virtuality == RHS->getVirtuality() &&
           VirtualIndex == RHS->getVirtualIndex() && Flags == RHS->getFlags() &&
           IsOptimized == RHS->isOptimized() &&
           Function == RHS->getRawFunction() &&
           TemplateParams == RHS->getRawTemplateParams() &&
           Declaration == RHS->getRawDeclaration() &&
           Variables == RHS->getRawVariables();
  }
  unsigned getHashValue() const {
    return hash_combine(Scope, Name, LinkageName, File, Line, Type,
                        IsLocalToUnit, IsDefinition, ScopeLine, ContainingType,
                        Virtuality, VirtualIndex, Flags, IsOptimized, Function,
                        TemplateParams, Declaration, Variables);
  }
};

template <> struct MDNodeKeyImpl<MDLexicalBlock> {
  Metadata *Scope;
  Metadata *File;
  unsigned Line;
  unsigned Column;

  MDNodeKeyImpl(Metadata *Scope, Metadata *File, unsigned Line, unsigned Column)
      : Scope(Scope), File(File), Line(Line), Column(Column) {}
  MDNodeKeyImpl(const MDLexicalBlock *N)
      : Scope(N->getRawScope()), File(N->getRawFile()), Line(N->getLine()),
        Column(N->getColumn()) {}

  bool isKeyOf(const MDLexicalBlock *RHS) const {
    return Scope == RHS->getRawScope() && File == RHS->getRawFile() &&
           Line == RHS->getLine() && Column == RHS->getColumn();
  }
  unsigned getHashValue() const {
    return hash_combine(Scope, File, Line, Column);
  }
};

template <> struct MDNodeKeyImpl<MDLexicalBlockFile> {
  Metadata *Scope;
  Metadata *File;
  unsigned Discriminator;

  MDNodeKeyImpl(Metadata *Scope, Metadata *File, unsigned Discriminator)
      : Scope(Scope), File(File), Discriminator(Discriminator) {}
  MDNodeKeyImpl(const MDLexicalBlockFile *N)
      : Scope(N->getRawScope()), File(N->getRawFile()),
        Discriminator(N->getDiscriminator()) {}

  bool isKeyOf(const MDLexicalBlockFile *RHS) const {
    return Scope == RHS->getRawScope() && File == RHS->getRawFile() &&
           Discriminator == RHS->getDiscriminator();
  }
  unsigned getHashValue() const {
    return hash_combine(Scope, File, Discriminator);
  }
};

template <> struct MDNodeKeyImpl<MDNamespace> {
  Metadata *Scope;
  Metadata *File;
  StringRef Name;
  unsigned Line;

  MDNodeKeyImpl(Metadata *Scope, Metadata *File, StringRef Name, unsigned Line)
      : Scope(Scope), File(File), Name(Name), Line(Line) {}
  MDNodeKeyImpl(const MDNamespace *N)
      : Scope(N->getRawScope()), File(N->getRawFile()), Name(N->getName()),
        Line(N->getLine()) {}

  bool isKeyOf(const MDNamespace *RHS) const {
    return Scope == RHS->getRawScope() && File == RHS->getRawFile() &&
           Name == RHS->getName() && Line == RHS->getLine();
  }
  unsigned getHashValue() const {
    return hash_combine(Scope, File, Name, Line);
  }
};

template <> struct MDNodeKeyImpl<MDTemplateTypeParameter> {
  StringRef Name;
  Metadata *Type;

  MDNodeKeyImpl(StringRef Name, Metadata *Type) : Name(Name), Type(Type) {}
  MDNodeKeyImpl(const MDTemplateTypeParameter *N)
      : Name(N->getName()), Type(N->getRawType()) {}

  bool isKeyOf(const MDTemplateTypeParameter *RHS) const {
    return Name == RHS->getName() && Type == RHS->getRawType();
  }
  unsigned getHashValue() const { return hash_combine(Name, Type); }
};

template <> struct MDNodeKeyImpl<MDTemplateValueParameter> {
  unsigned Tag;
  StringRef Name;
  Metadata *Type;
  Metadata *Value;

  MDNodeKeyImpl(unsigned Tag, StringRef Name, Metadata *Type, Metadata *Value)
      : Tag(Tag), Name(Name), Type(Type), Value(Value) {}
  MDNodeKeyImpl(const MDTemplateValueParameter *N)
      : Tag(N->getTag()), Name(N->getName()), Type(N->getRawType()),
        Value(N->getValue()) {}

  bool isKeyOf(const MDTemplateValueParameter *RHS) const {
    return Tag == RHS->getTag() && Name == RHS->getName() &&
           Type == RHS->getRawType() && Value == RHS->getValue();
  }
  unsigned getHashValue() const { return hash_combine(Tag, Name, Type, Value); }
};

template <> struct MDNodeKeyImpl<MDGlobalVariable> {
  Metadata *Scope;
  StringRef Name;
  StringRef LinkageName;
  Metadata *File;
  unsigned Line;
  Metadata *Type;
  bool IsLocalToUnit;
  bool IsDefinition;
  Metadata *Variable;
  Metadata *StaticDataMemberDeclaration;

  MDNodeKeyImpl(Metadata *Scope, StringRef Name, StringRef LinkageName,
                Metadata *File, unsigned Line, Metadata *Type,
                bool IsLocalToUnit, bool IsDefinition, Metadata *Variable,
                Metadata *StaticDataMemberDeclaration)
      : Scope(Scope), Name(Name), LinkageName(LinkageName), File(File),
        Line(Line), Type(Type), IsLocalToUnit(IsLocalToUnit),
        IsDefinition(IsDefinition), Variable(Variable),
        StaticDataMemberDeclaration(StaticDataMemberDeclaration) {}
  MDNodeKeyImpl(const MDGlobalVariable *N)
      : Scope(N->getRawScope()), Name(N->getName()),
        LinkageName(N->getLinkageName()), File(N->getRawFile()),
        Line(N->getLine()), Type(N->getRawType()),
        IsLocalToUnit(N->isLocalToUnit()), IsDefinition(N->isDefinition()),
        Variable(N->getRawVariable()),
        StaticDataMemberDeclaration(N->getRawStaticDataMemberDeclaration()) {}

  bool isKeyOf(const MDGlobalVariable *RHS) const {
    return Scope == RHS->getRawScope() && Name == RHS->getName() &&
           LinkageName == RHS->getLinkageName() && File == RHS->getRawFile() &&
           Line == RHS->getLine() && Type == RHS->getRawType() &&
           IsLocalToUnit == RHS->isLocalToUnit() &&
           IsDefinition == RHS->isDefinition() &&
           Variable == RHS->getRawVariable() &&
           StaticDataMemberDeclaration ==
               RHS->getRawStaticDataMemberDeclaration();
  }
  unsigned getHashValue() const {
    return hash_combine(Scope, Name, LinkageName, File, Line, Type,
                        IsLocalToUnit, IsDefinition, Variable,
                        StaticDataMemberDeclaration);
  }
};

template <> struct MDNodeKeyImpl<MDLocalVariable> {
  unsigned Tag;
  Metadata *Scope;
  StringRef Name;
  Metadata *File;
  unsigned Line;
  Metadata *Type;
  unsigned Arg;
  unsigned Flags;

  MDNodeKeyImpl(unsigned Tag, Metadata *Scope, StringRef Name, Metadata *File,
                unsigned Line, Metadata *Type, unsigned Arg, unsigned Flags)
      : Tag(Tag), Scope(Scope), Name(Name), File(File), Line(Line), Type(Type),
        Arg(Arg), Flags(Flags) {}
  MDNodeKeyImpl(const MDLocalVariable *N)
      : Tag(N->getTag()), Scope(N->getRawScope()), Name(N->getName()),
        File(N->getRawFile()), Line(N->getLine()), Type(N->getRawType()),
        Arg(N->getArg()), Flags(N->getFlags()) {}

  bool isKeyOf(const MDLocalVariable *RHS) const {
    return Tag == RHS->getTag() && Scope == RHS->getRawScope() &&
           Name == RHS->getName() && File == RHS->getRawFile() &&
           Line == RHS->getLine() && Type == RHS->getRawType() &&
           Arg == RHS->getArg() && Flags == RHS->getFlags();
  }
  unsigned getHashValue() const {
    return hash_combine(Tag, Scope, Name, File, Line, Type, Arg, Flags);
  }
};

template <> struct MDNodeKeyImpl<MDExpression> {
  ArrayRef<uint64_t> Elements;

  MDNodeKeyImpl(ArrayRef<uint64_t> Elements) : Elements(Elements) {}
  MDNodeKeyImpl(const MDExpression *N) : Elements(N->getElements()) {}

  bool isKeyOf(const MDExpression *RHS) const {
    return Elements == RHS->getElements();
  }
  unsigned getHashValue() const {
    return hash_combine_range(Elements.begin(), Elements.end());
  }
};

template <> struct MDNodeKeyImpl<MDObjCProperty> {
  StringRef Name;
  Metadata *File;
  unsigned Line;
  StringRef GetterName;
  StringRef SetterName;
  unsigned Attributes;
  Metadata *Type;

  MDNodeKeyImpl(StringRef Name, Metadata *File, unsigned Line,
                StringRef GetterName, StringRef SetterName, unsigned Attributes,
                Metadata *Type)
      : Name(Name), File(File), Line(Line), GetterName(GetterName),
        SetterName(SetterName), Attributes(Attributes), Type(Type) {}
  MDNodeKeyImpl(const MDObjCProperty *N)
      : Name(N->getName()), File(N->getRawFile()), Line(N->getLine()),
        GetterName(N->getGetterName()), SetterName(N->getSetterName()),
        Attributes(N->getAttributes()), Type(N->getRawType()) {}

  bool isKeyOf(const MDObjCProperty *RHS) const {
    return Name == RHS->getName() && File == RHS->getRawFile() &&
           Line == RHS->getLine() && GetterName == RHS->getGetterName() &&
           SetterName == RHS->getSetterName() &&
           Attributes == RHS->getAttributes() && Type == RHS->getRawType();
  }
  unsigned getHashValue() const {
    return hash_combine(Name, File, Line, GetterName, SetterName, Attributes,
                        Type);
  }
};

template <> struct MDNodeKeyImpl<MDImportedEntity> {
  unsigned Tag;
  Metadata *Scope;
  Metadata *Entity;
  unsigned Line;
  StringRef Name;

  MDNodeKeyImpl(unsigned Tag, Metadata *Scope, Metadata *Entity, unsigned Line,
                StringRef Name)
      : Tag(Tag), Scope(Scope), Entity(Entity), Line(Line), Name(Name) {}
  MDNodeKeyImpl(const MDImportedEntity *N)
      : Tag(N->getTag()), Scope(N->getRawScope()), Entity(N->getRawEntity()),
        Line(N->getLine()), Name(N->getName()) {}

  bool isKeyOf(const MDImportedEntity *RHS) const {
    return Tag == RHS->getTag() && Scope == RHS->getRawScope() &&
           Entity == RHS->getRawEntity() && Line == RHS->getLine() &&
           Name == RHS->getName();
  }
  unsigned getHashValue() const {
    return hash_combine(Tag, Scope, Entity, Line, Name);
  }
};

/// \brief DenseMapInfo for MDNode subclasses.
template <class NodeTy> struct MDNodeInfo {
  typedef MDNodeKeyImpl<NodeTy> KeyTy;
  static inline NodeTy *getEmptyKey() {
    return DenseMapInfo<NodeTy *>::getEmptyKey();
  }
  static inline NodeTy *getTombstoneKey() {
    return DenseMapInfo<NodeTy *>::getTombstoneKey();
  }
  static unsigned getHashValue(const KeyTy &Key) { return Key.getHashValue(); }
  static unsigned getHashValue(const NodeTy *N) {
    return KeyTy(N).getHashValue();
  }
  static bool isEqual(const KeyTy &LHS, const NodeTy *RHS) {
    if (RHS == getEmptyKey() || RHS == getTombstoneKey())
      return false;
    return LHS.isKeyOf(RHS);
  }
  static bool isEqual(const NodeTy *LHS, const NodeTy *RHS) {
    return LHS == RHS;
  }
};

#define HANDLE_MDNODE_LEAF(CLASS) typedef MDNodeInfo<CLASS> CLASS##Info;
#include "llvm/IR/Metadata.def"

/// \brief Map-like storage for metadata attachments.
class MDAttachmentMap {
  SmallVector<std::pair<unsigned, TrackingMDNodeRef>, 2> Attachments;

public:
  bool empty() const { return Attachments.empty(); }
  size_t size() const { return Attachments.size(); }

  /// \brief Get a particular attachment (if any).
  MDNode *lookup(unsigned ID) const;

  /// \brief Set an attachment to a particular node.
  ///
  /// Set the \c ID attachment to \c MD, replacing the current attachment at \c
  /// ID (if anyway).
  void set(unsigned ID, MDNode &MD);

  /// \brief Remove an attachment.
  ///
  /// Remove the attachment at \c ID, if any.
  void erase(unsigned ID);

  /// \brief Copy out all the attachments.
  ///
  /// Copies all the current attachments into \c Result, sorting by attachment
  /// ID.  This function does \em not clear \c Result.
  void getAll(SmallVectorImpl<std::pair<unsigned, MDNode *>> &Result) const;

  /// \brief Erase matching attachments.
  ///
  /// Erases all attachments matching the \c shouldRemove predicate.
  template <class PredTy> void remove_if(PredTy shouldRemove) {
    Attachments.erase(
        std::remove_if(Attachments.begin(), Attachments.end(), shouldRemove),
        Attachments.end());
  }
};

class LLVMContextImpl {
public:
  /// OwnedModules - The set of modules instantiated in this context, and which
  /// will be automatically deleted if this context is deleted.
  SmallPtrSet<Module*, 4> OwnedModules;
  
  LLVMContext::InlineAsmDiagHandlerTy InlineAsmDiagHandler;
  void *InlineAsmDiagContext;

  LLVMContext::DiagnosticHandlerTy DiagnosticHandler;
  void *DiagnosticContext;
  bool RespectDiagnosticFilters;

  LLVMContext::YieldCallbackTy YieldCallback;
  void *YieldOpaqueHandle;

  typedef DenseMap<APInt, ConstantInt *, DenseMapAPIntKeyInfo> IntMapTy;
  IntMapTy IntConstants;

  typedef DenseMap<APFloat, ConstantFP *, DenseMapAPFloatKeyInfo> FPMapTy;
  FPMapTy FPConstants;

  FoldingSet<AttributeImpl> AttrsSet;
  FoldingSet<AttributeSetImpl> AttrsLists;
  FoldingSet<AttributeSetNode> AttrsSetNodes;

  StringMap<MDString> MDStringCache;
  DenseMap<Value *, ValueAsMetadata *> ValuesAsMetadata;
  DenseMap<Metadata *, MetadataAsValue *> MetadataAsValues;

#define HANDLE_MDNODE_LEAF(CLASS) DenseSet<CLASS *, CLASS##Info> CLASS##s;
#include "llvm/IR/Metadata.def"

  // MDNodes may be uniqued or not uniqued.  When they're not uniqued, they
  // aren't in the MDNodeSet, but they're still shared between objects, so no
  // one object can destroy them.  This set allows us to at least destroy them
  // on Context destruction.
  SmallPtrSet<MDNode *, 1> DistinctMDNodes;

  DenseMap<Type*, ConstantAggregateZero*> CAZConstants;

  typedef ConstantUniqueMap<ConstantArray> ArrayConstantsTy;
  ArrayConstantsTy ArrayConstants;
  
  typedef ConstantUniqueMap<ConstantStruct> StructConstantsTy;
  StructConstantsTy StructConstants;

  typedef std::tuple<std::string, uint64_t, int64_t> CMPLookupKey;
  typedef std::tuple<CMPLookupKey, ConstantMemberPointer*> CMPElement;
  typedef std::map<CMPLookupKey, ConstantMemberPointer*> MemberPointerConstantsTy;
  MemberPointerConstantsTy MemberPointerConstants;
  
  typedef ConstantUniqueMap<ConstantVector> VectorConstantsTy;
  VectorConstantsTy VectorConstants;
  
  DenseMap<PointerType*, ConstantPointerNull*> CPNConstants;

  DenseMap<Type*, UndefValue*> UVConstants;
  
  StringMap<ConstantDataSequential*> CDSConstants;

  DenseMap<std::pair<const Function *, const BasicBlock *>, BlockAddress *>
    BlockAddresses;
  ConstantUniqueMap<ConstantExpr> ExprConstants;

  ConstantUniqueMap<InlineAsm> InlineAsms;

  ConstantInt *TheTrueVal;
  ConstantInt *TheFalseVal;

  // Basic type instances.
  Type VoidTy, LabelTy, HalfTy, FloatTy, DoubleTy, MetadataTy;
  Type X86_FP80Ty, FP128Ty, PPC_FP128Ty, X86_MMXTy;
  IntegerType Int1Ty, Int8Ty, Int16Ty, Int32Ty, Int64Ty, Int128Ty;

  
  /// TypeAllocator - All dynamically allocated types are allocated from this.
  /// They live forever until the context is torn down.
  BumpPtrAllocator TypeAllocator;
  
  DenseMap<unsigned, IntegerType*> IntegerTypes;

  typedef DenseSet<FunctionType *, FunctionTypeKeyInfo> FunctionTypeSet;
  FunctionTypeSet FunctionTypes;
  typedef DenseSet<StructType *, AnonStructTypeKeyInfo> StructTypeSet;
  StructTypeSet AnonStructTypes;
  StringMap<StructType*> NamedStructTypes;
  unsigned NamedStructTypesUniqueID;
    
  DenseMap<std::pair<Type *, uint64_t>, ArrayType*> ArrayTypes;
  DenseMap<std::pair<Type *, unsigned>, VectorType*> VectorTypes;
  DenseMap<Type*, PointerType*> PointerTypes;  // Pointers in AddrSpace = 0
  DenseMap<std::pair<Type*, unsigned>, PointerType*> ASPointerTypes;


  /// ValueHandles - This map keeps track of all of the value handles that are
  /// watching a Value*.  The Value::HasValueHandle bit is used to know
  /// whether or not a value has an entry in this map.
  typedef DenseMap<Value*, ValueHandleBase*> ValueHandlesTy;
  ValueHandlesTy ValueHandles;
  
  /// CustomMDKindNames - Map to hold the metadata string to ID mapping.
  StringMap<unsigned> CustomMDKindNames;

  /// Collection of per-instruction metadata used in this context.
  DenseMap<const Instruction *, MDAttachmentMap> InstructionMetadata;

  /// Collection of per-function metadata used in this context.
  DenseMap<const Function *, MDAttachmentMap> FunctionMetadata;

  /// DiscriminatorTable - This table maps file:line locations to an
  /// integer representing the next DWARF path discriminator to assign to
  /// instructions in different blocks at the same location.
  DenseMap<std::pair<const char *, unsigned>, unsigned> DiscriminatorTable;

  /// IntrinsicIDCache - Cache of intrinsic name (string) to numeric ID mappings
  /// requested in this context
  typedef DenseMap<const Function*, unsigned> IntrinsicIDCacheTy;
  IntrinsicIDCacheTy IntrinsicIDCache;

  /// \brief Mapping from a function to its prefix data, which is stored as the
  /// operand of an unparented ReturnInst so that the prefix data has a Use.
  typedef DenseMap<const Function *, ReturnInst *> PrefixDataMapTy;
  PrefixDataMapTy PrefixDataMap;

  /// \brief Mapping from a function to its prologue data, which is stored as
  /// the operand of an unparented ReturnInst so that the prologue data has a
  /// Use.
  typedef DenseMap<const Function *, ReturnInst *> PrologueDataMapTy;
  PrologueDataMapTy PrologueDataMap;

  int getOrAddScopeRecordIdxEntry(MDNode *N, int ExistingIdx);
  int getOrAddScopeInlinedAtIdxEntry(MDNode *Scope, MDNode *IA,int ExistingIdx);

  LLVMContextImpl(LLVMContext &C);
  ~LLVMContextImpl();

  /// Destroy the ConstantArrays if they are not used.
  void dropTriviallyDeadConstantArrays();
};

}

#endif

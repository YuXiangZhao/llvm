//===- CodeViewYAMLTypes.cpp - CodeView YAMLIO types implementation -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines classes for handling the YAML representation of CodeView
// Debug Info.
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjectYAML/CodeViewYAMLTypes.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/DebugInfo/CodeView/CVTypeVisitor.h"
#include "llvm/DebugInfo/CodeView/CodeViewError.h"
#include "llvm/DebugInfo/CodeView/EnumTables.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeTableBuilder.h"
#include "llvm/Support/BinaryStreamWriter.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::CodeViewYAML;
using namespace llvm::CodeViewYAML::detail;
using namespace llvm::yaml;

LLVM_YAML_IS_SEQUENCE_VECTOR(OneMethodRecord)
LLVM_YAML_IS_SEQUENCE_VECTOR(StringRef)
LLVM_YAML_IS_SEQUENCE_VECTOR(VFTableSlotKind)
LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(TypeIndex)

LLVM_YAML_DECLARE_SCALAR_TRAITS(TypeIndex, false)
LLVM_YAML_DECLARE_SCALAR_TRAITS(APSInt, false)

LLVM_YAML_DECLARE_ENUM_TRAITS(TypeLeafKind)
LLVM_YAML_DECLARE_ENUM_TRAITS(PointerToMemberRepresentation)
LLVM_YAML_DECLARE_ENUM_TRAITS(VFTableSlotKind)
LLVM_YAML_DECLARE_ENUM_TRAITS(CallingConvention)
LLVM_YAML_DECLARE_ENUM_TRAITS(PointerKind)
LLVM_YAML_DECLARE_ENUM_TRAITS(PointerMode)
LLVM_YAML_DECLARE_ENUM_TRAITS(HfaKind)
LLVM_YAML_DECLARE_ENUM_TRAITS(MemberAccess)
LLVM_YAML_DECLARE_ENUM_TRAITS(MethodKind)
LLVM_YAML_DECLARE_ENUM_TRAITS(WindowsRTClassKind)
LLVM_YAML_DECLARE_ENUM_TRAITS(LabelType)

LLVM_YAML_DECLARE_BITSET_TRAITS(PointerOptions)
LLVM_YAML_DECLARE_BITSET_TRAITS(ModifierOptions)
LLVM_YAML_DECLARE_BITSET_TRAITS(FunctionOptions)
LLVM_YAML_DECLARE_BITSET_TRAITS(ClassOptions)
LLVM_YAML_DECLARE_BITSET_TRAITS(MethodOptions)

LLVM_YAML_DECLARE_MAPPING_TRAITS(OneMethodRecord)
LLVM_YAML_DECLARE_MAPPING_TRAITS(MemberPointerInfo)

namespace llvm {
namespace CodeViewYAML {
namespace detail {

struct LeafRecordBase {
  TypeLeafKind Kind;
  explicit LeafRecordBase(TypeLeafKind K) : Kind(K) {}

  virtual ~LeafRecordBase() {}
  virtual void map(yaml::IO &io) = 0;
  virtual CVType toCodeViewRecord(TypeTableBuilder &TTB) const = 0;
  virtual Error fromCodeViewRecord(CVType Type) = 0;
};

template <typename T> struct LeafRecordImpl : public LeafRecordBase {
  explicit LeafRecordImpl(TypeLeafKind K)
      : LeafRecordBase(K), Record(static_cast<TypeRecordKind>(K)) {}

  void map(yaml::IO &io) override;

  Error fromCodeViewRecord(CVType Type) override {
    return TypeDeserializer::deserializeAs<T>(Type, Record);
  }

  CVType toCodeViewRecord(TypeTableBuilder &TTB) const override {
    TTB.writeKnownType(Record);
    return CVType(Kind, TTB.records().back());
  }

  mutable T Record;
};

template <> struct LeafRecordImpl<FieldListRecord> : public LeafRecordBase {
  explicit LeafRecordImpl(TypeLeafKind K) : LeafRecordBase(K) {}

  void map(yaml::IO &io) override;
  CVType toCodeViewRecord(TypeTableBuilder &TTB) const override;
  Error fromCodeViewRecord(CVType Type) override;

  std::vector<MemberRecord> Members;
};

struct MemberRecordBase {
  TypeLeafKind Kind;
  explicit MemberRecordBase(TypeLeafKind K) : Kind(K) {}

  virtual ~MemberRecordBase() {}
  virtual void map(yaml::IO &io) = 0;
  virtual void writeTo(FieldListRecordBuilder &FLRB) = 0;
};

template <typename T> struct MemberRecordImpl : public MemberRecordBase {
  explicit MemberRecordImpl(TypeLeafKind K)
      : MemberRecordBase(K), Record(static_cast<TypeRecordKind>(K)) {}
  void map(yaml::IO &io) override;

  void writeTo(FieldListRecordBuilder &FLRB) override {
    FLRB.writeMemberType(Record);
  }

  mutable T Record;
};
}
}
}

void ScalarTraits<TypeIndex>::output(const TypeIndex &S, void *,
                                     llvm::raw_ostream &OS) {
  OS << S.getIndex();
}

StringRef ScalarTraits<TypeIndex>::input(StringRef Scalar, void *Ctx,
                                         TypeIndex &S) {
  uint32_t I;
  StringRef Result = ScalarTraits<uint32_t>::input(Scalar, Ctx, I);
  S.setIndex(I);
  return Result;
}

void ScalarTraits<APSInt>::output(const APSInt &S, void *,
                                  llvm::raw_ostream &OS) {
  S.print(OS, true);
}

StringRef ScalarTraits<APSInt>::input(StringRef Scalar, void *Ctx, APSInt &S) {
  S = APSInt(Scalar);
  return "";
}

void ScalarEnumerationTraits<TypeLeafKind>::enumeration(IO &io,
                                                        TypeLeafKind &Value) {
#define CV_TYPE(name, val) io.enumCase(Value, #name, name);
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"
#undef CV_TYPE
}

void ScalarEnumerationTraits<PointerToMemberRepresentation>::enumeration(
    IO &IO, PointerToMemberRepresentation &Value) {
  IO.enumCase(Value, "Unknown", PointerToMemberRepresentation::Unknown);
  IO.enumCase(Value, "SingleInheritanceData",
              PointerToMemberRepresentation::SingleInheritanceData);
  IO.enumCase(Value, "MultipleInheritanceData",
              PointerToMemberRepresentation::MultipleInheritanceData);
  IO.enumCase(Value, "VirtualInheritanceData",
              PointerToMemberRepresentation::VirtualInheritanceData);
  IO.enumCase(Value, "GeneralData", PointerToMemberRepresentation::GeneralData);
  IO.enumCase(Value, "SingleInheritanceFunction",
              PointerToMemberRepresentation::SingleInheritanceFunction);
  IO.enumCase(Value, "MultipleInheritanceFunction",
              PointerToMemberRepresentation::MultipleInheritanceFunction);
  IO.enumCase(Value, "VirtualInheritanceFunction",
              PointerToMemberRepresentation::VirtualInheritanceFunction);
  IO.enumCase(Value, "GeneralFunction",
              PointerToMemberRepresentation::GeneralFunction);
}

void ScalarEnumerationTraits<VFTableSlotKind>::enumeration(
    IO &IO, VFTableSlotKind &Kind) {
  IO.enumCase(Kind, "Near16", VFTableSlotKind::Near16);
  IO.enumCase(Kind, "Far16", VFTableSlotKind::Far16);
  IO.enumCase(Kind, "This", VFTableSlotKind::This);
  IO.enumCase(Kind, "Outer", VFTableSlotKind::Outer);
  IO.enumCase(Kind, "Meta", VFTableSlotKind::Meta);
  IO.enumCase(Kind, "Near", VFTableSlotKind::Near);
  IO.enumCase(Kind, "Far", VFTableSlotKind::Far);
}

void ScalarEnumerationTraits<CallingConvention>::enumeration(
    IO &IO, CallingConvention &Value) {
  IO.enumCase(Value, "NearC", CallingConvention::NearC);
  IO.enumCase(Value, "FarC", CallingConvention::FarC);
  IO.enumCase(Value, "NearPascal", CallingConvention::NearPascal);
  IO.enumCase(Value, "FarPascal", CallingConvention::FarPascal);
  IO.enumCase(Value, "NearFast", CallingConvention::NearFast);
  IO.enumCase(Value, "FarFast", CallingConvention::FarFast);
  IO.enumCase(Value, "NearStdCall", CallingConvention::NearStdCall);
  IO.enumCase(Value, "FarStdCall", CallingConvention::FarStdCall);
  IO.enumCase(Value, "NearSysCall", CallingConvention::NearSysCall);
  IO.enumCase(Value, "FarSysCall", CallingConvention::FarSysCall);
  IO.enumCase(Value, "ThisCall", CallingConvention::ThisCall);
  IO.enumCase(Value, "MipsCall", CallingConvention::MipsCall);
  IO.enumCase(Value, "Generic", CallingConvention::Generic);
  IO.enumCase(Value, "AlphaCall", CallingConvention::AlphaCall);
  IO.enumCase(Value, "PpcCall", CallingConvention::PpcCall);
  IO.enumCase(Value, "SHCall", CallingConvention::SHCall);
  IO.enumCase(Value, "ArmCall", CallingConvention::ArmCall);
  IO.enumCase(Value, "AM33Call", CallingConvention::AM33Call);
  IO.enumCase(Value, "TriCall", CallingConvention::TriCall);
  IO.enumCase(Value, "SH5Call", CallingConvention::SH5Call);
  IO.enumCase(Value, "M32RCall", CallingConvention::M32RCall);
  IO.enumCase(Value, "ClrCall", CallingConvention::ClrCall);
  IO.enumCase(Value, "Inline", CallingConvention::Inline);
  IO.enumCase(Value, "NearVector", CallingConvention::NearVector);
}

void ScalarEnumerationTraits<PointerKind>::enumeration(IO &IO,
                                                       PointerKind &Kind) {
  IO.enumCase(Kind, "Near16", PointerKind::Near16);
  IO.enumCase(Kind, "Far16", PointerKind::Far16);
  IO.enumCase(Kind, "Huge16", PointerKind::Huge16);
  IO.enumCase(Kind, "BasedOnSegment", PointerKind::BasedOnSegment);
  IO.enumCase(Kind, "BasedOnValue", PointerKind::BasedOnValue);
  IO.enumCase(Kind, "BasedOnSegmentValue", PointerKind::BasedOnSegmentValue);
  IO.enumCase(Kind, "BasedOnAddress", PointerKind::BasedOnAddress);
  IO.enumCase(Kind, "BasedOnSegmentAddress",
              PointerKind::BasedOnSegmentAddress);
  IO.enumCase(Kind, "BasedOnType", PointerKind::BasedOnType);
  IO.enumCase(Kind, "BasedOnSelf", PointerKind::BasedOnSelf);
  IO.enumCase(Kind, "Near32", PointerKind::Near32);
  IO.enumCase(Kind, "Far32", PointerKind::Far32);
  IO.enumCase(Kind, "Near64", PointerKind::Near64);
}

void ScalarEnumerationTraits<PointerMode>::enumeration(IO &IO,
                                                       PointerMode &Mode) {
  IO.enumCase(Mode, "Pointer", PointerMode::Pointer);
  IO.enumCase(Mode, "LValueReference", PointerMode::LValueReference);
  IO.enumCase(Mode, "PointerToDataMember", PointerMode::PointerToDataMember);
  IO.enumCase(Mode, "PointerToMemberFunction",
              PointerMode::PointerToMemberFunction);
  IO.enumCase(Mode, "RValueReference", PointerMode::RValueReference);
}

void ScalarEnumerationTraits<HfaKind>::enumeration(IO &IO, HfaKind &Value) {
  IO.enumCase(Value, "None", HfaKind::None);
  IO.enumCase(Value, "Float", HfaKind::Float);
  IO.enumCase(Value, "Double", HfaKind::Double);
  IO.enumCase(Value, "Other", HfaKind::Other);
}

void ScalarEnumerationTraits<MemberAccess>::enumeration(IO &IO,
                                                        MemberAccess &Access) {
  IO.enumCase(Access, "None", MemberAccess::None);
  IO.enumCase(Access, "Private", MemberAccess::Private);
  IO.enumCase(Access, "Protected", MemberAccess::Protected);
  IO.enumCase(Access, "Public", MemberAccess::Public);
}

void ScalarEnumerationTraits<MethodKind>::enumeration(IO &IO,
                                                      MethodKind &Kind) {
  IO.enumCase(Kind, "Vanilla", MethodKind::Vanilla);
  IO.enumCase(Kind, "Virtual", MethodKind::Virtual);
  IO.enumCase(Kind, "Static", MethodKind::Static);
  IO.enumCase(Kind, "Friend", MethodKind::Friend);
  IO.enumCase(Kind, "IntroducingVirtual", MethodKind::IntroducingVirtual);
  IO.enumCase(Kind, "PureVirtual", MethodKind::PureVirtual);
  IO.enumCase(Kind, "PureIntroducingVirtual",
              MethodKind::PureIntroducingVirtual);
}

void ScalarEnumerationTraits<WindowsRTClassKind>::enumeration(
    IO &IO, WindowsRTClassKind &Value) {
  IO.enumCase(Value, "None", WindowsRTClassKind::None);
  IO.enumCase(Value, "Ref", WindowsRTClassKind::RefClass);
  IO.enumCase(Value, "Value", WindowsRTClassKind::ValueClass);
  IO.enumCase(Value, "Interface", WindowsRTClassKind::Interface);
}

void ScalarEnumerationTraits<LabelType>::enumeration(IO &IO, LabelType &Value) {
  IO.enumCase(Value, "Near", LabelType::Near);
  IO.enumCase(Value, "Far", LabelType::Far);
}

void ScalarBitSetTraits<PointerOptions>::bitset(IO &IO,
                                                PointerOptions &Options) {
  IO.bitSetCase(Options, "None", PointerOptions::None);
  IO.bitSetCase(Options, "Flat32", PointerOptions::Flat32);
  IO.bitSetCase(Options, "Volatile", PointerOptions::Volatile);
  IO.bitSetCase(Options, "Const", PointerOptions::Const);
  IO.bitSetCase(Options, "Unaligned", PointerOptions::Unaligned);
  IO.bitSetCase(Options, "Restrict", PointerOptions::Restrict);
  IO.bitSetCase(Options, "WinRTSmartPointer",
                PointerOptions::WinRTSmartPointer);
}

void ScalarBitSetTraits<ModifierOptions>::bitset(IO &IO,
                                                 ModifierOptions &Options) {
  IO.bitSetCase(Options, "None", ModifierOptions::None);
  IO.bitSetCase(Options, "Const", ModifierOptions::Const);
  IO.bitSetCase(Options, "Volatile", ModifierOptions::Volatile);
  IO.bitSetCase(Options, "Unaligned", ModifierOptions::Unaligned);
}

void ScalarBitSetTraits<FunctionOptions>::bitset(IO &IO,
                                                 FunctionOptions &Options) {
  IO.bitSetCase(Options, "None", FunctionOptions::None);
  IO.bitSetCase(Options, "CxxReturnUdt", FunctionOptions::CxxReturnUdt);
  IO.bitSetCase(Options, "Constructor", FunctionOptions::Constructor);
  IO.bitSetCase(Options, "ConstructorWithVirtualBases",
                FunctionOptions::ConstructorWithVirtualBases);
}

void ScalarBitSetTraits<ClassOptions>::bitset(IO &IO, ClassOptions &Options) {
  IO.bitSetCase(Options, "None", ClassOptions::None);
  IO.bitSetCase(Options, "HasConstructorOrDestructor",
                ClassOptions::HasConstructorOrDestructor);
  IO.bitSetCase(Options, "HasOverloadedOperator",
                ClassOptions::HasOverloadedOperator);
  IO.bitSetCase(Options, "Nested", ClassOptions::Nested);
  IO.bitSetCase(Options, "ContainsNestedClass",
                ClassOptions::ContainsNestedClass);
  IO.bitSetCase(Options, "HasOverloadedAssignmentOperator",
                ClassOptions::HasOverloadedAssignmentOperator);
  IO.bitSetCase(Options, "HasConversionOperator",
                ClassOptions::HasConversionOperator);
  IO.bitSetCase(Options, "ForwardReference", ClassOptions::ForwardReference);
  IO.bitSetCase(Options, "Scoped", ClassOptions::Scoped);
  IO.bitSetCase(Options, "HasUniqueName", ClassOptions::HasUniqueName);
  IO.bitSetCase(Options, "Sealed", ClassOptions::Sealed);
  IO.bitSetCase(Options, "Intrinsic", ClassOptions::Intrinsic);
}

void ScalarBitSetTraits<MethodOptions>::bitset(IO &IO, MethodOptions &Options) {
  IO.bitSetCase(Options, "None", MethodOptions::None);
  IO.bitSetCase(Options, "Pseudo", MethodOptions::Pseudo);
  IO.bitSetCase(Options, "NoInherit", MethodOptions::NoInherit);
  IO.bitSetCase(Options, "NoConstruct", MethodOptions::NoConstruct);
  IO.bitSetCase(Options, "CompilerGenerated", MethodOptions::CompilerGenerated);
  IO.bitSetCase(Options, "Sealed", MethodOptions::Sealed);
}

void MappingTraits<MemberPointerInfo>::mapping(IO &IO, MemberPointerInfo &MPI) {
  IO.mapRequired("ContainingType", MPI.ContainingType);
  IO.mapRequired("Representation", MPI.Representation);
}

namespace llvm {
namespace CodeViewYAML {
namespace detail {
template <> void LeafRecordImpl<ModifierRecord>::map(IO &IO) {
  IO.mapRequired("ModifiedType", Record.ModifiedType);
  IO.mapRequired("Modifiers", Record.Modifiers);
}

template <> void LeafRecordImpl<ProcedureRecord>::map(IO &IO) {
  IO.mapRequired("ReturnType", Record.ReturnType);
  IO.mapRequired("CallConv", Record.CallConv);
  IO.mapRequired("Options", Record.Options);
  IO.mapRequired("ParameterCount", Record.ParameterCount);
  IO.mapRequired("ArgumentList", Record.ArgumentList);
}

template <> void LeafRecordImpl<MemberFunctionRecord>::map(IO &IO) {
  IO.mapRequired("ReturnType", Record.ReturnType);
  IO.mapRequired("ClassType", Record.ClassType);
  IO.mapRequired("ThisType", Record.ThisType);
  IO.mapRequired("CallConv", Record.CallConv);
  IO.mapRequired("Options", Record.Options);
  IO.mapRequired("ParameterCount", Record.ParameterCount);
  IO.mapRequired("ArgumentList", Record.ArgumentList);
  IO.mapRequired("ThisPointerAdjustment", Record.ThisPointerAdjustment);
}

template <> void LeafRecordImpl<LabelRecord>::map(IO &IO) {
  IO.mapRequired("Mode", Record.Mode);
}

template <> void LeafRecordImpl<MemberFuncIdRecord>::map(IO &IO) {
  IO.mapRequired("ClassType", Record.ClassType);
  IO.mapRequired("FunctionType", Record.FunctionType);
  IO.mapRequired("Name", Record.Name);
}

template <> void LeafRecordImpl<ArgListRecord>::map(IO &IO) {
  IO.mapRequired("ArgIndices", Record.ArgIndices);
}

template <> void LeafRecordImpl<StringListRecord>::map(IO &IO) {
  IO.mapRequired("StringIndices", Record.StringIndices);
}

template <> void LeafRecordImpl<PointerRecord>::map(IO &IO) {
  IO.mapRequired("ReferentType", Record.ReferentType);
  IO.mapRequired("Attrs", Record.Attrs);
  IO.mapOptional("MemberInfo", Record.MemberInfo);
}

template <> void LeafRecordImpl<ArrayRecord>::map(IO &IO) {
  IO.mapRequired("ElementType", Record.ElementType);
  IO.mapRequired("IndexType", Record.IndexType);
  IO.mapRequired("Size", Record.Size);
  IO.mapRequired("Name", Record.Name);
}

void LeafRecordImpl<FieldListRecord>::map(IO &IO) {
  IO.mapRequired("FieldList", Members);
}
}
}
}

namespace {
class MemberRecordConversionVisitor : public TypeVisitorCallbacks {
public:
  explicit MemberRecordConversionVisitor(std::vector<MemberRecord> &Records)
      : Records(Records) {}

#define TYPE_RECORD(EnumName, EnumVal, Name)
#define MEMBER_RECORD(EnumName, EnumVal, Name)                                 \
  Error visitKnownMember(CVMemberRecord &CVR, Name##Record &Record) override { \
    return visitKnownMemberImpl(Record);                                       \
  }
#define TYPE_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#define MEMBER_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"
private:
  template <typename T> Error visitKnownMemberImpl(T &Record) {
    TypeLeafKind K = static_cast<TypeLeafKind>(Record.getKind());
    auto Impl = std::make_shared<MemberRecordImpl<T>>(K);
    Impl->Record = Record;
    Records.push_back(MemberRecord{Impl});
    return Error::success();
  }

  std::vector<MemberRecord> &Records;
};
}

Error LeafRecordImpl<FieldListRecord>::fromCodeViewRecord(CVType Type) {
  MemberRecordConversionVisitor V(Members);
  return visitMemberRecordStream(Type.content(), V);
}

CVType
LeafRecordImpl<FieldListRecord>::toCodeViewRecord(TypeTableBuilder &TTB) const {
  FieldListRecordBuilder FLRB(TTB);
  FLRB.begin();
  for (const auto &Member : Members) {
    Member.Member->writeTo(FLRB);
  }
  FLRB.end(true);
  return CVType(Kind, TTB.records().back());
}

void MappingTraits<OneMethodRecord>::mapping(IO &io, OneMethodRecord &Record) {
  io.mapRequired("Type", Record.Type);
  io.mapRequired("Attrs", Record.Attrs.Attrs);
  io.mapRequired("VFTableOffset", Record.VFTableOffset);
  io.mapRequired("Name", Record.Name);
}

namespace llvm {
namespace CodeViewYAML {
namespace detail {
template <> void LeafRecordImpl<ClassRecord>::map(IO &IO) {
  IO.mapRequired("MemberCount", Record.MemberCount);
  IO.mapRequired("Options", Record.Options);
  IO.mapRequired("FieldList", Record.FieldList);
  IO.mapRequired("Name", Record.Name);
  IO.mapRequired("UniqueName", Record.UniqueName);

  IO.mapRequired("DerivationList", Record.DerivationList);
  IO.mapRequired("VTableShape", Record.VTableShape);
  IO.mapRequired("Size", Record.Size);
}

template <> void LeafRecordImpl<UnionRecord>::map(IO &IO) {
  IO.mapRequired("MemberCount", Record.MemberCount);
  IO.mapRequired("Options", Record.Options);
  IO.mapRequired("FieldList", Record.FieldList);
  IO.mapRequired("Name", Record.Name);
  IO.mapRequired("UniqueName", Record.UniqueName);

  IO.mapRequired("Size", Record.Size);
}

template <> void LeafRecordImpl<EnumRecord>::map(IO &IO) {
  IO.mapRequired("NumEnumerators", Record.MemberCount);
  IO.mapRequired("Options", Record.Options);
  IO.mapRequired("FieldList", Record.FieldList);
  IO.mapRequired("Name", Record.Name);
  IO.mapRequired("UniqueName", Record.UniqueName);

  IO.mapRequired("UnderlyingType", Record.UnderlyingType);
}

template <> void LeafRecordImpl<BitFieldRecord>::map(IO &IO) {
  IO.mapRequired("Type", Record.Type);
  IO.mapRequired("BitSize", Record.BitSize);
  IO.mapRequired("BitOffset", Record.BitOffset);
}

template <> void LeafRecordImpl<VFTableShapeRecord>::map(IO &IO) {
  IO.mapRequired("Slots", Record.Slots);
}

template <> void LeafRecordImpl<TypeServer2Record>::map(IO &IO) {
  IO.mapRequired("Guid", Record.Guid);
  IO.mapRequired("Age", Record.Age);
  IO.mapRequired("Name", Record.Name);
}

template <> void LeafRecordImpl<StringIdRecord>::map(IO &IO) {
  IO.mapRequired("Id", Record.Id);
  IO.mapRequired("String", Record.String);
}

template <> void LeafRecordImpl<FuncIdRecord>::map(IO &IO) {
  IO.mapRequired("ParentScope", Record.ParentScope);
  IO.mapRequired("FunctionType", Record.FunctionType);
  IO.mapRequired("Name", Record.Name);
}

template <> void LeafRecordImpl<UdtSourceLineRecord>::map(IO &IO) {
  IO.mapRequired("UDT", Record.UDT);
  IO.mapRequired("SourceFile", Record.SourceFile);
  IO.mapRequired("LineNumber", Record.LineNumber);
}

template <> void LeafRecordImpl<UdtModSourceLineRecord>::map(IO &IO) {
  IO.mapRequired("UDT", Record.UDT);
  IO.mapRequired("SourceFile", Record.SourceFile);
  IO.mapRequired("LineNumber", Record.LineNumber);
  IO.mapRequired("Module", Record.Module);
}

template <> void LeafRecordImpl<BuildInfoRecord>::map(IO &IO) {
  IO.mapRequired("ArgIndices", Record.ArgIndices);
}

template <> void LeafRecordImpl<VFTableRecord>::map(IO &IO) {
  IO.mapRequired("CompleteClass", Record.CompleteClass);
  IO.mapRequired("OverriddenVFTable", Record.OverriddenVFTable);
  IO.mapRequired("VFPtrOffset", Record.VFPtrOffset);
  IO.mapRequired("MethodNames", Record.MethodNames);
}

template <> void LeafRecordImpl<MethodOverloadListRecord>::map(IO &IO) {
  IO.mapRequired("Methods", Record.Methods);
}

template <> void MemberRecordImpl<OneMethodRecord>::map(IO &IO) {
  MappingTraits<OneMethodRecord>::mapping(IO, Record);
}

template <> void MemberRecordImpl<OverloadedMethodRecord>::map(IO &IO) {
  IO.mapRequired("NumOverloads", Record.NumOverloads);
  IO.mapRequired("MethodList", Record.MethodList);
  IO.mapRequired("Name", Record.Name);
}

template <> void MemberRecordImpl<NestedTypeRecord>::map(IO &IO) {
  IO.mapRequired("Type", Record.Type);
  IO.mapRequired("Name", Record.Name);
}

template <> void MemberRecordImpl<DataMemberRecord>::map(IO &IO) {
  IO.mapRequired("Attrs", Record.Attrs.Attrs);
  IO.mapRequired("Type", Record.Type);
  IO.mapRequired("FieldOffset", Record.FieldOffset);
  IO.mapRequired("Name", Record.Name);
}

template <> void MemberRecordImpl<StaticDataMemberRecord>::map(IO &IO) {
  IO.mapRequired("Attrs", Record.Attrs.Attrs);
  IO.mapRequired("Type", Record.Type);
  IO.mapRequired("Name", Record.Name);
}

template <> void MemberRecordImpl<EnumeratorRecord>::map(IO &IO) {
  IO.mapRequired("Attrs", Record.Attrs.Attrs);
  IO.mapRequired("Value", Record.Value);
  IO.mapRequired("Name", Record.Name);
}

template <> void MemberRecordImpl<VFPtrRecord>::map(IO &IO) {
  IO.mapRequired("Type", Record.Type);
}

template <> void MemberRecordImpl<BaseClassRecord>::map(IO &IO) {
  IO.mapRequired("Attrs", Record.Attrs.Attrs);
  IO.mapRequired("Type", Record.Type);
  IO.mapRequired("Offset", Record.Offset);
}

template <> void MemberRecordImpl<VirtualBaseClassRecord>::map(IO &IO) {
  IO.mapRequired("Attrs", Record.Attrs.Attrs);
  IO.mapRequired("BaseType", Record.BaseType);
  IO.mapRequired("VBPtrType", Record.VBPtrType);
  IO.mapRequired("VBPtrOffset", Record.VBPtrOffset);
  IO.mapRequired("VTableIndex", Record.VTableIndex);
}

template <> void MemberRecordImpl<ListContinuationRecord>::map(IO &IO) {
  IO.mapRequired("ContinuationIndex", Record.ContinuationIndex);
}
}
}
}

template <typename T>
static inline Expected<LeafRecord> fromCodeViewRecordImpl(CVType Type) {
  LeafRecord Result;

  auto Impl = std::make_shared<LeafRecordImpl<T>>(Type.kind());
  if (auto EC = Impl->fromCodeViewRecord(Type))
    return std::move(EC);
  Result.Leaf = Impl;
  return Result;
}

Expected<LeafRecord> LeafRecord::fromCodeViewRecord(CVType Type) {
#define TYPE_RECORD(EnumName, EnumVal, ClassName)                              \
  case EnumName:                                                               \
    return fromCodeViewRecordImpl<ClassName##Record>(Type);
#define TYPE_RECORD_ALIAS(EnumName, EnumVal, AliasName, ClassName)             \
  TYPE_RECORD(EnumName, EnumVal, ClassName)
#define MEMBER_RECORD(EnumName, EnumVal, ClassName)
#define MEMBER_RECORD_ALIAS(EnumName, EnumVal, AliasName, ClassName)
  switch (Type.kind()) {
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"
  default: { llvm_unreachable("Unknown leaf kind!"); }
  }
  return make_error<CodeViewError>(cv_error_code::corrupt_record);
}

CVType LeafRecord::toCodeViewRecord(BumpPtrAllocator &Alloc) const {
  TypeTableBuilder TTB(Alloc);
  return Leaf->toCodeViewRecord(TTB);
}

CVType LeafRecord::toCodeViewRecord(TypeTableBuilder &TTB) const {
  return Leaf->toCodeViewRecord(TTB);
}

namespace llvm {
namespace yaml {
template <> struct MappingTraits<LeafRecordBase> {
  static void mapping(IO &io, LeafRecordBase &Record) { Record.map(io); }
};

template <> struct MappingTraits<MemberRecordBase> {
  static void mapping(IO &io, MemberRecordBase &Record) { Record.map(io); }
};
}
}

template <typename ConcreteType>
static void mapLeafRecordImpl(IO &IO, const char *Class, TypeLeafKind Kind,
                              LeafRecord &Obj) {
  if (!IO.outputting())
    Obj.Leaf = std::make_shared<LeafRecordImpl<ConcreteType>>(Kind);

  if (Kind == LF_FIELDLIST)
    Obj.Leaf->map(IO);
  else
    IO.mapRequired(Class, *Obj.Leaf);
}

void MappingTraits<LeafRecord>::mapping(IO &IO, LeafRecord &Obj) {
  TypeLeafKind Kind;
  if (IO.outputting())
    Kind = Obj.Leaf->Kind;
  IO.mapRequired("Kind", Kind);

#define TYPE_RECORD(EnumName, EnumVal, ClassName)                              \
  case EnumName:                                                               \
    mapLeafRecordImpl<ClassName##Record>(IO, #ClassName, Kind, Obj);           \
    break;
#define TYPE_RECORD_ALIAS(EnumName, EnumVal, AliasName, ClassName)             \
  TYPE_RECORD(EnumName, EnumVal, ClassName)
#define MEMBER_RECORD(EnumName, EnumVal, ClassName)
#define MEMBER_RECORD_ALIAS(EnumName, EnumVal, AliasName, ClassName)
  switch (Kind) {
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"
  default: { llvm_unreachable("Unknown leaf kind!"); }
  }
}

template <typename ConcreteType>
static void mapMemberRecordImpl(IO &IO, const char *Class, TypeLeafKind Kind,
                                MemberRecord &Obj) {
  if (!IO.outputting())
    Obj.Member = std::make_shared<MemberRecordImpl<ConcreteType>>(Kind);

  IO.mapRequired(Class, *Obj.Member);
}

void MappingTraits<MemberRecord>::mapping(IO &IO, MemberRecord &Obj) {
  TypeLeafKind Kind;
  if (IO.outputting())
    Kind = Obj.Member->Kind;
  IO.mapRequired("Kind", Kind);

#define MEMBER_RECORD(EnumName, EnumVal, ClassName)                            \
  case EnumName:                                                               \
    mapMemberRecordImpl<ClassName##Record>(IO, #ClassName, Kind, Obj);         \
    break;
#define MEMBER_RECORD_ALIAS(EnumName, EnumVal, AliasName, ClassName)           \
  MEMBER_RECORD(EnumName, EnumVal, ClassName)
#define TYPE_RECORD(EnumName, EnumVal, ClassName)
#define TYPE_RECORD_ALIAS(EnumName, EnumVal, AliasName, ClassName)
  switch (Kind) {
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"
  default: { llvm_unreachable("Unknown member kind!"); }
  }
}

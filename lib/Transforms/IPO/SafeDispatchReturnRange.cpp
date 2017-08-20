//===-- SafeDispatchReturnRange.cpp - SafeDispatch ReturnRange code ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the SDReturnRange class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/SafeDispatchReturnRange.h"

#include "llvm/IR/DebugInfo.h"
#include "llvm/Transforms/IPO/SafeDispatchTools.h"

#include <cxxabi.h>
#include <fstream>
#include <sstream>

using namespace llvm;

char SDReturnRange::ID = 0;

INITIALIZE_PASS(SDReturnRange,
"sdRetRange", "Build return ranges", false, false)

const std::string SDReturnRange::VTABLE_DEMANGLE_PREFIX = "vtable for ";
const std::string SDReturnRange::CONSTRUCTION_VTABLE_DEMANGLE_PREFIX = "construction vtable for ";

ModulePass *llvm::createSDReturnRangePass() {
  return new SDReturnRange();
}

//TODO MATT: format properly / code duplication
static StringRef sd_getClassNameFromMD(llvm::MDNode *MDNode, unsigned operandNo = 0) {
  llvm::MDTuple *mdTuple = cast<llvm::MDTuple>(MDNode);
  assert(mdTuple->getNumOperands() > operandNo + 1);

  llvm::MDNode *nameMdNode = cast<llvm::MDNode>(mdTuple->getOperand(operandNo).get());
  llvm::MDString *mdStr = cast<llvm::MDString>(nameMdNode->getOperand(0));

  StringRef strRef = mdStr->getString();
  assert(sd_isVtableName_ref(strRef));
  return strRef;
}

static StringRef sd_getFunctionNameFromMD(llvm::MDNode *MDNode, unsigned operandNo = 0) {
  assert(MDNode->getNumOperands() > operandNo);

  llvm::MDString *mdStr = cast<llvm::MDString>(MDNode->getOperand(operandNo));

  StringRef strRef = mdStr->getString();
  return strRef;
}

bool SDReturnRange::runOnModule(Module &M) {
  // Get the results from the class hierarchy analysis pass.
  CHA = &getAnalysis<SDBuildCHA>();

  sdLog::blankLine();
  sdLog::stream() << "P7a. Started running the SDReturnRange pass ..." << sdLog::newLine << "\n";

  CHA->buildFunctionInfo();

  locateCallSites(M);
  locateStaticCallSites(M);

  // Store the data generated by this pass.
  storeCallSites(M);
  storeClassHierarchy(M);

  sdLog::stream() << sdLog::newLine << "P7a. Finished running the SDReturnRange pass ..." << "\n";
  sdLog::blankLine();
  return false;
}

void SDReturnRange::locateCallSites(Module &M) {
  Function *IntrinsicFunction = M.getFunction(Intrinsic::getName(Intrinsic::sd_get_checked_vptr));

  if (IntrinsicFunction == nullptr) {
    sdLog::warn() << "Intrinsic not found.\n";
    return;
  }

  for (const Use &U : IntrinsicFunction->uses()) {

    // get the intrinsic call instruction
    CallInst *IntrinsicCall = dyn_cast<CallInst>(U.getUser());
    assert(IntrinsicCall && "Intrinsic was not wrapped in a CallInst?");

    // Find the CallSite that is associated with the intrinsic call.
    User *User = *(IntrinsicCall->users().begin());
    for (int i = 0; i < 3; ++i) {
      // User was not found, this should not happen...
      if (User == nullptr)
        break;

      for (auto *NextUser : User->users()) {
        User = NextUser;
        break;
      }
    }

    if (CallInst *CallSite = dyn_cast<CallInst>(User)) {
      // valid CallSite
      addCallSite(IntrinsicCall, CallSite, M);
    } else {
      sdLog::warn() << "CallSite for intrinsic was not found.\n";
      IntrinsicCall->getParent()->dump();
    }

    sdLog::log() << "\n";
  }
}

static bool isRelevantStaticFunction(const Function &F) {
  return !(F.getName().startswith("__") || F.getName().startswith("llvm.") || F.getName() == "_Znwm");

}

void SDReturnRange::locateStaticCallSites(Module &M) {
  for (auto &F : M) {
    for(auto &MBB : F) {
      for (auto &I : MBB) {
        if (auto *Call = dyn_cast<CallInst>(&I)) {
          if (Function *F = Call->getCalledFunction()) {
            if (isRelevantStaticFunction(*F)){
              addStaticCallSite(Call, M);
            }
          }
        }
      }
    }
  }
}

void SDReturnRange::addStaticCallSite(CallInst *CallSite, Module &M) {
  const DebugLoc &Loc = CallSite->getDebugLoc();
  if (!Loc) {
    // Minor hack: We generate our own DebugLoc using a dummy MDSubprogram.
    // pseudoDebugLoc is the unique ID for this CallSite.
    llvm::LLVMContext &C = M.getContext();
    auto DummyProgram = MDSubprogram::getDistinct(C, nullptr, "", "", nullptr, 0,
                                                  nullptr, false, false, 0, nullptr, 0, 0, 0,
                                                  0);
    MDLocation *Location = MDLocation::get(C, pseudoDebugLoc / 65536, pseudoDebugLoc % 65536, DummyProgram);
    ++pseudoDebugLoc;
    DebugLoc newLoc(Location);
    CallSite->setDebugLoc(Location);
  }

  // write DebugLoc to map (is written to file in storeCallSites)
  auto *Scope = cast<MDScope>(Loc.getScope());
  std::string FunctionName = CallSite->getCalledFunction()->getName().str();
  std::stringstream Stream;
  Stream << Scope->getFilename().str() << ":" << Loc.getLine() << ":" << Loc.getCol()
         << "," << FunctionName;

  CallSiteDebugLocsStatic.push_back(Stream.str());
  CalledFunctions.insert(FunctionName);

  sdLog::log() << "CallSite " << CallSite->getParent()->getParent()->getName()
                  << " @" << Scope->getFilename().str() << ":" << Loc.getLine() << ":" << Loc.getCol()
                  << " for callee " << FunctionName << "\n";

}

void SDReturnRange::addCallSite(const CallInst *CheckedVptrCall, CallInst *CallSite, Module &M) {
  // get ClassName metadata
  MetadataAsValue *Arg2 = dyn_cast<MetadataAsValue>(CheckedVptrCall->getArgOperand(1));
  assert(Arg2);
  MDNode *ClassNameNode = dyn_cast<MDNode>(Arg2->getMetadata());
  assert(ClassNameNode);

  // get PreciseName metadata
  MetadataAsValue *Arg3 = dyn_cast<MetadataAsValue>(CheckedVptrCall->getArgOperand(2));
  assert(Arg3);
  MDNode *PreciseNameNode = dyn_cast<MDNode>(Arg2->getMetadata());
  assert(PreciseNameNode);

  // get PreciseName metadata
  MetadataAsValue *Arg4 = dyn_cast<MetadataAsValue>(CheckedVptrCall->getArgOperand(3));
  assert(Arg4);
  MDNode *FunctionNameNode = dyn_cast<MDNode>(Arg4->getMetadata());
  assert(FunctionNameNode);

  // Arg2 is the tuple that contains the class name and the corresponding global var.
  // note that the global variable isn't always emitted

  const StringRef ClassName = sd_getClassNameFromMD(ClassNameNode);
  const StringRef PreciseName = sd_getClassNameFromMD(PreciseNameNode);
  const StringRef FunctionName = sd_getFunctionNameFromMD(FunctionNameNode);

  const DebugLoc &Loc = CallSite->getDebugLoc();
  if (!Loc) {
    // Minor hack: We generate our own DebugLoc using a dummy MDSubprogram.
    // pseudoDebugLoc is the unique ID for this CallSite.
    llvm::LLVMContext &C = M.getContext();
    auto DummyProgram = MDSubprogram::getDistinct(C, nullptr, "", "", nullptr, 0,
                                                  nullptr, false, false, 0, nullptr, 0, 0, 0,
                                                  0);
    MDLocation *Location = MDLocation::get(C, pseudoDebugLoc / 65536, pseudoDebugLoc % 65536, DummyProgram);
    ++pseudoDebugLoc;
    DebugLoc newLoc(Location);
    CallSite->setDebugLoc(Location);
  }

  // write DebugLoc to map (is written to file in storeCallSites)
  auto *Scope = cast<MDScope>(Loc.getScope());
  std::stringstream Stream;
  Stream << Scope->getFilename().str() << ":" << Loc.getLine() << ":" << Loc.getCol()
         << "," << ClassName.str() << "," << PreciseName.str() << "," << FunctionName.str();

  CallSiteDebugLocs.push_back(Stream.str());

  sdLog::log() << "CallSite @" << Scope->getFilename().str() << ":" << Loc.getLine() << ":" << Loc.getCol()
                  << " for class " << ClassName << "(" << PreciseName << ")::" << FunctionName << "\n";

  // emit a SubclassHierarchy for ClassName if it's not already emitted
  emitSubclassHierarchyIfNeeded(ClassName);
}

std::string SDReturnRange::demangleVtableToClassName(StringRef VTableName) {
  int status = 0;
  // Call the external reference demangler
  std::unique_ptr<char, void (*)(void *)> res{
          abi::__cxa_demangle(VTableName.str().c_str(), NULL, NULL, &status),
          std::free
  };

  if (status != 0) {
    sdLog::warn() << "Demangle failed for " << VTableName << " with status: " << status << "\n";
    return "";
  }

  StringRef demangledName = res.get();

  if (demangledName.startswith(VTABLE_DEMANGLE_PREFIX)) {
    // default case: VTableName is a normal itanium vtable.
    return demangledName.drop_front(VTABLE_DEMANGLE_PREFIX.length());
  }
  if (demangledName.startswith(CONSTRUCTION_VTABLE_DEMANGLE_PREFIX)) {
    // VTableName is a construction itanium vtable (A in B vtable) -> Not "really" a class
    return "";
  }

  // Unknown demangle?
  sdLog::errs() << "Demangle with unknown format " << VTableName << " to: " << demangledName << "\n";
  llvm_unreachable("Demangle of unknown type!");
  //return demangledName;
}

void SDReturnRange::createSubclassHierarchy(const SDBuildCHA::vtbl_t &Root, std::set <std::string> &Output) {
  for (auto it = CHA->children_begin(Root); it != CHA->children_end(Root); it++) {
    const SDBuildCHA::vtbl_t &Child = *it;

    std::string ClassName = demangleVtableToClassName(Child.first);
    if (ClassName == "")
      continue;

    Output.insert(Child.first);
    MangledClassName[ClassName] = Child.first;
    createSubclassHierarchy(Child, Output);

  }
}

void SDReturnRange::emitSubclassHierarchyIfNeeded(StringRef RootVtableName) {
  // Already emitted?
  if (ClassHierarchies.find(RootVtableName) != ClassHierarchies.end())
    return;

  const std::string RootClassName = demangleVtableToClassName(RootVtableName);
  if (RootClassName == "") {
    // Failed to demangle...
    sdLog::warn() << "Emitting hierarchy failed for " << RootVtableName << "\n";
    return;
  }
  sdLog::log() << "Emitting hierarchy for " << RootClassName << " (" << RootVtableName << "): ";

  // Traverse through subvtables and search for subclasses. (RootClass is a subclass as well)
  std::set <std::string> SubclassSet;
  SubclassSet.insert(RootVtableName.str());
  MangledClassName[RootClassName] = RootVtableName.str();
  createSubclassHierarchy(SDBuildCHA::vtbl_t(RootVtableName, 0), SubclassSet);

  // Store hierarchy.
  ClassHierarchies[RootVtableName] = SubclassSet;

  bool first = true;
  for (auto &element: SubclassSet) {
    if (first) {
      sdLog::logNoToken() << element;
      first = false;
    } else {
      sdLog::logNoToken() << ", " << element;
    }
  }
  sdLog::logNoToken() << "\n";
}

void SDReturnRange::storeCallSites(Module &M) {
  sdLog::stream() << "Store all callsites for module: " << M.getName() << "\n";
  std::ofstream Outfile("./_SD_CallSites.txt");
  std::ostream_iterator <std::string> OutIterator(Outfile, "\n");
  std::copy(CallSiteDebugLocs.begin(), CallSiteDebugLocs.end(), OutIterator);
  Outfile.close();

  int number = 0;
  auto outName = "./_SD_CallSites" + std::to_string(number);
  std::ifstream infile(outName);
  while(infile.good()) {
    number++;
    outName = "./_SD_CallSites" + std::to_string(number);
    infile = std::ifstream(outName);
  }

  std::ifstream  src("./_SD_CallSites.txt", std::ios::binary);
  std::ofstream  dst(outName, std::ios::binary);
  dst << src.rdbuf();



  std::ofstream Outfile2("./_SD_CallSitesStatic.txt");
  std::ostream_iterator <std::string> OutIterator2(Outfile2, "\n");
  std::copy(CallSiteDebugLocsStatic.begin(), CallSiteDebugLocsStatic.end(), OutIterator2);
  Outfile2.close();

  outName = "./_SD_CallSitesStatic" + std::to_string(number);
  std::ifstream  src2("./_SD_CallSitesStatic.txt", std::ios::binary);
  std::ofstream  dst2(outName, std::ios::binary);
  dst2 << src2.rdbuf();
}

void SDReturnRange::storeClassHierarchy(Module &M) {
  sdLog::stream() << "Store relevant class hierarchies for module: " << M.getName() << "\n";
  std::ofstream Outfile("./_SD_ClassHierarchy.txt");
  for (auto &mapEntry : ClassHierarchies) {
    Outfile << mapEntry.first;
    for (auto &element : mapEntry.second) {
      Outfile << "," << element;
    }
    Outfile << "\n";
  }

  int number = 0;
  auto outName = "./_SD_ClassHierarchy" + std::to_string(number);
  std::ifstream infile(outName);
  while(infile.good()) {
    number++;
    outName = "./_SD_ClassHierarchy" + std::to_string(number);
    infile = std::ifstream(outName);
  }

  std::ifstream  src("./_SD_ClassHierarchy.txt", std::ios::binary);
  std::ofstream  dst(outName, std::ios::binary);
  dst << src.rdbuf();
}
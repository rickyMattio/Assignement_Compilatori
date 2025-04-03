#include "llvm/IR/Instructions.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/ADT/APInt.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include <vector>
using namespace llvm;

namespace {

// Algebraic Identity


// Funzione helper
bool isSpecificInt(Value *V, int64_t val) {
    if (ConstantInt *C = dyn_cast<ConstantInt>(V)) {
        return C->getValue().getSExtValue() == val;
    }
    return false;
}

struct AlgebraicIdentity : PassInfoMixin<AlgebraicIdentity> {

    bool runOnBasicBlock(BasicBlock &B) {

        bool ChangedInBlock = false;
        std::vector<Instruction*> InstructionsToRemove;
	// Iteriamo sulle istruzione nel blocco
        for (Instruction &I : B) {


            if (BinaryOperator *BinOp = dyn_cast<BinaryOperator>(&I)) {
                Value *Op0 = BinOp->getOperand(0);
                Value *Op1 = BinOp->getOperand(1);
                Value *OtherOperand = nullptr;

                if (BinOp->getOpcode() == Instruction::Add) {
                    if (isSpecificInt(Op1, 0)) OtherOperand = Op0;
                    else if (isSpecificInt(Op0, 0)) OtherOperand = Op1;
                } else if (BinOp->getOpcode() == Instruction::Mul) {
                    if (isSpecificInt(Op1, 1)) OtherOperand = Op0;
                    else if (isSpecificInt(Op0, 1)) OtherOperand = Op1;
                }

                if (OtherOperand) {
                    I.replaceAllUsesWith(OtherOperand);
                    InstructionsToRemove.push_back(&I);
                    ChangedInBlock = true;
                }
            }
        }

        for (Instruction *I : InstructionsToRemove) { I->eraseFromParent(); }
        return ChangedInBlock;
    }

    bool runOnFunction(Function &F) {
        bool Transformed = false;
        for (auto &BB : F) { if (runOnBasicBlock(BB)) { Transformed = true; } }
        return Transformed;
    }

    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
        if (runOnFunction(F)) { return PreservedAnalyses::none(); }
        return PreservedAnalyses::all();
    }
    static bool isRequired() { return true; }
};


// Strength Reduction

struct StrengthReduction : PassInfoMixin<StrengthReduction> {

     bool runOnBasicBlock(BasicBlock &B) {
        bool ChangedInBlock = false;
        std::vector<Instruction*> InstructionsToRemove;

        for (Instruction &I : B) {
            if (BinaryOperator *BinOp = dyn_cast<BinaryOperator>(&I)) {
                Value *Op0 = BinOp->getOperand(0);
                Value *Op1 = BinOp->getOperand(1);
                Value *OtherOperand = nullptr;

                //MUL con 15
                if (BinOp->getOpcode() == Instruction::Mul) {
                    ConstantInt *ConstOp = nullptr;
                    if (ConstantInt *C = dyn_cast<ConstantInt>(Op1)) { if (C->getValue().getSExtValue() == 15) { ConstOp = C; OtherOperand = Op0; } }
                    else if (ConstantInt *C = dyn_cast<ConstantInt>(Op0)) { if (C->getValue().getSExtValue() == 15) { ConstOp = C; OtherOperand = Op1; } }

                    if (ConstOp) {
                        Instruction *Shift = BinaryOperator::Create(Instruction::Shl, OtherOperand, ConstantInt::get(OtherOperand->getType(), 4), "shl4");
                        Shift->insertBefore(&I);
                        Instruction *Sub = BinaryOperator::Create(Instruction::Sub, Shift, OtherOperand, "subx");
                        Sub->insertBefore(&I);
                        I.replaceAllUsesWith(Sub);
                        InstructionsToRemove.push_back(&I);
                        ChangedInBlock = true;
                    }
                }
                // SDIV con 8
                else if (BinOp->getOpcode() == Instruction::SDiv) {
                     Value* Divisor = Op1;
                     if (ConstantInt *C = dyn_cast<ConstantInt>(Divisor)) {
                         if (C->getValue().getSExtValue() == 8) {
                             Value* Dividend = Op0;
                             unsigned ShiftAmount = C->getValue().exactLogBase2();
                             Instruction *Shift = BinaryOperator::Create(Instruction::AShr, Dividend, ConstantInt::get(Dividend->getType(), ShiftAmount), "ashr3");
                             Shift->insertBefore(&I);
                             I.replaceAllUsesWith(Shift);
                             InstructionsToRemove.push_back(&I);
                             ChangedInBlock = true;
                         }
                     }
                }
            }
        }

        for (Instruction *I : InstructionsToRemove) { I->eraseFromParent(); }
        return ChangedInBlock;
    }

    bool runOnFunction(Function &F) {
        bool Transformed = false;
        for (auto &BB : F) { if (runOnBasicBlock(BB)) { Transformed = true; } }
        return Transformed;
    }

    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
        if (runOnFunction(F)) { return PreservedAnalyses::none(); }
        return PreservedAnalyses::all();
    }
    static bool isRequired() { return true; }
};

// Multi-Instruction Optimization

struct MultiInstructionOpt : PassInfoMixin<MultiInstructionOpt> {

     bool runOnBasicBlock(BasicBlock &B) {
        bool ChangedInBlock = false;
        std::vector<Instruction*> InstructionsToRemove;

        for (Instruction &I : B) {

		//Cerchiamo c = a - 1


            if (BinaryOperator *SubInst = dyn_cast<BinaryOperator>(&I)) {
                if (SubInst->getOpcode() == Instruction::Sub && isSpecificInt(SubInst->getOperand(1), 1)) {
                     // È una sottrazione di 1

		     Value *PossibleA = SubInst->getOperand(0);
		     // Controlliamo se 'a' viene da 'b + 1' o '1 + b'
                     if (Instruction *AddInst = dyn_cast<Instruction>(PossibleA)) {
                         if (AddInst->getParent() == &B && AddInst->getOpcode() == Instruction::Add) {
                             Value* PossibleB = nullptr;
                             if (isSpecificInt(AddInst->getOperand(1), 1)) { PossibleB = AddInst->getOperand(0); }
                             else if (isSpecificInt(AddInst->getOperand(0), 1)) { PossibleB = AddInst->getOperand(1); }
				// ECCOLOOOOO!
                             if (PossibleB && AddInst->hasOneUse()) {
                                 I.replaceAllUsesWith(PossibleB);
                                 InstructionsToRemove.push_back(&I);
                                 ChangedInBlock = true;
                             }
                         }
                     }
                }
            }
        }

        for (Instruction *I : InstructionsToRemove) { I->eraseFromParent(); }
        return ChangedInBlock;
    }

     bool runOnFunction(Function &F) {
        bool Transformed = false;
        for (auto &BB : F) { if (runOnBasicBlock(BB)) { Transformed = true; } }
        return Transformed;
    }

    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
        if (runOnFunction(F)) { return PreservedAnalyses::none(); }
        return PreservedAnalyses::all();
    }
    static bool isRequired() { return true; }
};

}


extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "CombinedOpts", LLVM_VERSION_STRING,
            [](PassBuilder &PB) {
                // Registra il primo passo
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "algebraic-identity") {
                            FPM.addPass(AlgebraicIdentity());
                            return true;
                        }
                        return false;
                    });
                // Registra il secondo passo
                 PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "strength-reduction") {
                            FPM.addPass(StrengthReduction());
                            return true;
                        }
                        return false;
                    });
                 // Registra il terzo passo
                 PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "multi-instruction-opt") {
                            FPM.addPass(MultiInstructionOpt());
                            return true;
                        }
                        return false;
                    });
            }};
}

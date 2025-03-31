#include "llvm/IR/Instructions.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Argument.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/ADT/APInt.h"
#include <vector>
using namespace llvm;

namespace {

struct CombinedOpts : PassInfoMixin<CombinedOpts> {

    // Funzione helper per controllare se un valore è una costante intera == val
    bool isSpecificInt(Value *V, int64_t val) {
        if (ConstantInt *C = dyn_cast<ConstantInt>(V)) {


            return C->getValue() == val;
        }


        return false;
    }

    bool runOnBasicBlock(BasicBlock &B) {

        bool ChangedInBlock = false;
        std::vector<Instruction*> InstructionsToRemove;

        // Iteriamo sulle istruzioni nel blocco
        for (Instruction &I : B) {

            //1. Algebraic Identity
            if (BinaryOperator *BinOp = dyn_cast<BinaryOperator>(&I)) {
                Value *Op0 = BinOp->getOperand(0);
                Value *Op1 = BinOp->getOperand(1);
                Value *OtherOperand = nullptr;

                //ADD con 0
                if (BinOp->getOpcode() == Instruction::Add) {
                    if (isSpecificInt(Op1, 0)) OtherOperand = Op0;
                    else if (isSpecificInt(Op0, 0)) OtherOperand = Op1;
                }


                //MUL con 1
                else if (BinOp->getOpcode() == Instruction::Mul) {
                    if (isSpecificInt(Op1, 1)) OtherOperand = Op0;

                    else if (isSpecificInt(Op0, 1)) OtherOperand = Op1;
                }

                if (OtherOperand) {

                    I.replaceAllUsesWith(OtherOperand);
                    InstructionsToRemove.push_back(&I);
                    ChangedInBlock = true;
                    continue; // Ottimizzazione applicata, passa alla prossima istruzione
                }

                //2. Strength Reduction
                //MUL con 15
                if (BinOp->getOpcode() == Instruction::Mul) {
                    ConstantInt *ConstOp = nullptr;
                    OtherOperand = nullptr;

                    if (ConstantInt *C = dyn_cast<ConstantInt>(Op1)) { if (C->getValue() == 15) { ConstOp = C; OtherOperand = Op0; } }
                    else if (ConstantInt *C = dyn_cast<ConstantInt>(Op0)) { if (C->getValue() == 15) { ConstOp = C; OtherOperand = Op1; } }

                    if (ConstOp) {
                        
                        Instruction *Shift = BinaryOperator::Create(Instruction::Shl, OtherOperand, ConstantInt::get(OtherOperand->getType(), 4), "shl4");
                        Shift->insertBefore(&I);
                        Instruction *Sub = BinaryOperator::Create(Instruction::Sub, Shift, OtherOperand, "subx");
                        Sub->insertBefore(&I); // Sub viene inserito DOPO Shift, ma PRIMA di I

                        I.replaceAllUsesWith(Sub);
                        InstructionsToRemove.push_back(&I);
                        ChangedInBlock = true;
                        continue; // Ottimizzazione applicata
                    }
                }
                // SDIV con 8
                else if (BinOp->getOpcode() == Instruction::SDiv) {
                    Value* Divisor = BinOp->getOperand(1);

                    if (ConstantInt *C = dyn_cast<ConstantInt>(Divisor)) {
                        if (C->getValue() == 8) {
                            
                            Value* Dividend = BinOp->getOperand(0);
                            Instruction *Shift = BinaryOperator::Create(Instruction::AShr, Dividend, ConstantInt::get(Dividend->getType(), 3), "ashr3");
                            Shift->insertBefore(&I);

                            I.replaceAllUsesWith(Shift);
                            InstructionsToRemove.push_back(&I);
                            ChangedInBlock = true;
                            continue; // Ottimizzazione applicata
                        }


                    }
                }

            } // Fine controlli su BinaryOperator

            //3. Multi-Instruction Optimization
            // Cerchiamo c = a - 1
            if (BinaryOperator *SubInst = dyn_cast<BinaryOperator>(&I)) {
                if (SubInst->getOpcode() == Instruction::Sub && isSpecificInt(SubInst->getOperand(1), 1)) {


                     // È una sottrazione di 1
                     Value *PossibleA = SubInst->getOperand(0);
                     // Controlliamo se 'a' viene da 'b + 1' o '1 + b'
                     if (Instruction *AddInst = dyn_cast<Instruction>(PossibleA)) {


                         if (AddInst->getOpcode() == Instruction::Add) {

                             Value* PossibleB = nullptr;

                             if (isSpecificInt(AddInst->getOperand(1), 1)) {

				PossibleB = AddInst->getOperand(0); // Caso b + 1
                             } else if (isSpecificInt(AddInst->getOperand(0), 1)) {
                                 PossibleB = AddInst->getOperand(1); // Caso 1 + b

                             }



                             if (PossibleB) {
                                 // ECCOLOOOOOO!

                                 I.replaceAllUsesWith(PossibleB); // Sostituisci c con b
                                 InstructionsToRemove.push_back(&I); // Rimuovi c = a - 1
                                 ChangedInBlock = true;
                                 // Non rimuoviamo AddInst (a=b+1)

                             }


                         }
                     }


                }




            }

        }

        // Rimuovi le istruzioni marcate
        for (Instruction *I : InstructionsToRemove) {
            I->eraseFromParent();
        }

        return ChangedInBlock;
    }


    bool runOnFunction(Function &F) {
        bool Transformed = false;
        for (auto &BB : F) {
            if (runOnBasicBlock(BB)) {
                Transformed = true;
            }
        }
        return Transformed;
    }


    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
        if (runOnFunction(F)) {
            return PreservedAnalyses::none();
        }
        return PreservedAnalyses::all();
    }


    static bool isRequired() { return true; }
};

}


extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "CombinedOpts", LLVM_VERSION_STRING,
            [](PassBuilder &PB) {
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "combined-opts") {
                            FPM.addPass(CombinedOpts());
                            return true;
                        }
                        return false;
                    });
            }};
}

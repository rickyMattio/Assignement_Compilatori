//=============================================================================
// FILE:
//    LoopInvariantPass.cpp
//
// DESCRIPTION:
//    Identifica le istruzioni loop invariant in un ciclo. 
//
// USAGE:
//    New PM
//      opt -load-pass-plugin=<path-to>/libLoopInvariantPass.so -passes="loop-invariant-pass" \
//          -disable-output <input-llvm-file>
//
// License: MIT
//=============================================================================

#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/Dominators.h"





using namespace llvm;


namespace {

bool isOperandLoopInvariant(Value *Op, Loop *L, SmallVectorImpl<Value *> &CheckedOperands); 

// Funzione di supporto per verificare se un'istruzione è loop invariant
void isLoopInvariant(Instruction &I, Loop *L, SmallVectorImpl<Value *> &CheckedOperands) {
    // Controlla se l'istruzione è già stata verificata
    if (std::find(CheckedOperands.begin(), CheckedOperands.end(), &I) != CheckedOperands.end()) {
        // Se l'istruzione è già nel vettore, è stata verificata come loop invariant
        return;
    }

    // Se è una phi, non può essere loop-invariant
    if (isa<PHINode>(&I)) {
        return;
    }

    // Se l'istruzione ha effetti collaterali (es. scrittura in memoria), non può essere loop invariant
    if (I.mayHaveSideEffects()) {
        return;
    }
    
    // Controlla tutti gli operandi dell'istruzione
    for (Value *Op : I.operands()) {
        if (!isOperandLoopInvariant(Op, L, CheckedOperands)) {
            return; // Se anche un solo operando non è loop invariant, esci
        }
    }

    // Se tutti gli operandi sono loop invariant e non ha side effects, allora l'istruzione è loop invariant
    // Aggiungi l'istruzione al vettore CheckedOperands
    CheckedOperands.push_back(&I);
}

bool isOperandLoopInvariant(Value *Op, Loop *L, SmallVectorImpl<Value *> &CheckedOperands) {
    // Controlla se l'operando è un'istruzione
    if (Instruction *RD = dyn_cast<Instruction>(Op)) {
        // Se l'istruzione NON è nel loop, allora è loop invariant
        if (!L->contains(RD)) {
            return true;
        }

        // Se è nel loop, allora è loop invariant solo se anche lei è loop invariant
        isLoopInvariant(*RD, L, CheckedOperands);
    }

    // Se l'operando NON è un'istruzione (es. costante), è loop invariant per definizione
    return true;
}


// Funzione per verificare se un blocco domina tutte le uscite del loop
void isInBlockDominatingAllExits(Instruction &I, Loop *L, SmallVectorImpl<Value *> &CheckedOperands,DominatorTree &DT) {
    // Ottieni il blocco che contiene l'istruzione
    BasicBlock *BB = I.getParent();

    // Ottieni tutti i blocchi di uscita del loop
    SmallVector<BasicBlock *, 8> ExitBlocks;
    L->getExitBlocks(ExitBlocks);

    // Verifica se il blocco domina tutte le uscite
    for (BasicBlock *Exit : ExitBlocks) {
        if(!DT.dominates(BB, Exit)) {
            // Se non domina rimuovi l'istruzione e termina
            auto It = std::find(CheckedOperands.begin(), CheckedOperands.end(), &I);
            if (It != CheckedOperands.end()) {
                CheckedOperands.erase(It);
            }
            return; // Termina la funzione
        }
    }
}

bool dominatesAllUsesInLoop(Instruction &I, Loop *L, DominatorTree &DT, const SmallPtrSetImpl<Instruction *> &InvariantSet) {
    BasicBlock *DefBlock = I.getParent();
    for (User *U : I.users()) {
        if (Instruction *UserInst = dyn_cast<Instruction>(U)) {
            // Se l'uso è nel ciclo, verifica che il blocco di definizione domini il blocco dell'uso
            if (L->contains(UserInst)) {
                if (!DT.dominates(DefBlock, UserInst->getParent())) {
                    return false;
                }
                // Verifica che l'uso sia già stato spostato nel preheader
                if (!InvariantSet.contains(UserInst)) {
                    return false;
                }
            }
        }
    }
    return true;
}


void dfsCodeMotion(Instruction *I,
    Loop *L,
    DominatorTree &DT,
    BasicBlock *Preheader,
    SmallPtrSetImpl<Instruction *> &Visited,
    SmallPtrSetImpl<Instruction *> &InvariantSet) {
    if (!Visited.insert(I).second) return; // già visitata

    // Visita ricorsivamente le dipendenze
    for (Value *Op : I->operands()) {
        if (Instruction *OpInst = dyn_cast<Instruction>(Op)) {
            if (InvariantSet.contains(OpInst)) {
                dfsCodeMotion(OpInst, L, DT, Preheader, Visited, InvariantSet);
            }
        }
    }

    // Dopo aver visitato le dipendenze, sposta se le condizioni sono rispettate
    if (dominatesAllUsesInLoop(*I, L, DT, InvariantSet)) {
        if (I->isTerminator()) {
            errs() << "NON SPOSTATA (terminator): " << *I << "\n";
        } else {
            // Sposta l'istruzione nel preheader
            I->moveBefore(Preheader->getTerminator());
            errs() << "Istruzione spostata nel preheader (dfs): " << *I << "\n";
        }
    } else {
        errs() << "NON SPOSTATA (domination failure): " << *I << "\n";
    }
}





// Pass per identificare le istruzioni loop invariant
struct LoopInvariantPass : PassInfoMixin<LoopInvariantPass> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
        // Ottieni LoopInfo per la funzione
        LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);
        DominatorTree &DT = FAM.getResult<DominatorTreeAnalysis>(F);

        

        // Itera su tutti i cicli nella funzione
        for (Loop *L : LI) {
            // Crea un vettore per memorizzare gli operandi già controllati come loop invariant
            SmallVector<Value *, 8> CheckedOperands;
            
            if (BasicBlock *Header = L->getHeader()) {
                errs() << "Analizzando il ciclo: " << Header->getName() << "\n";
            } else {
                errs() << "Ciclo senza header trovato.\n";
                continue;
            }
  
            
            // Itera su tutti i blocchi del ciclo
            for (BasicBlock *BB : L->blocks()) {
                // Itera su tutte le istruzioni nel blocco
                for (Instruction &I : *BB) {
                    
                    // Esegui isLoopInvariant per aggiornare CheckedOperands
                    isLoopInvariant(I, L, CheckedOperands);
                }
            }
            
            // Itera su tutte le istruzioni loop invariant salvate in CheckedOperands
            for (Value *Checked : CheckedOperands) {
                if (Instruction *CheckedInst = dyn_cast<Instruction>(Checked)) {
                    isInBlockDominatingAllExits(*CheckedInst, L, CheckedOperands, DT);
                }
            }

            
            BasicBlock *Preheader = L->getLoopPreheader();
            // Se non esiste un preheader, non possiamo spostare le istruzioni
            if (!Preheader) {
                errs() << "Nessun preheader trovato per il ciclo\n";
                continue;
            }
            // Costruisci l'insieme delle loop-invariant per accesso rapido
            SmallPtrSet<Instruction *, 8> InvariantSet;

            for (Value *Checked : CheckedOperands) {
                if (Instruction *CheckedInst = dyn_cast<Instruction>(Checked)) {
                    if (dominatesAllUsesInLoop(*CheckedInst, L, DT, InvariantSet)) {
                        InvariantSet.insert(CheckedInst);
                    } else {
                        errs() << "Istruzione non aggiunta a InvariantSet: " << *CheckedInst << "\n";
                    }
                }
            }

            // DFS bottom-up che sposta direttamente nel preheader
            SmallPtrSet<Instruction *, 8> Visited;

            for (Instruction *I : InvariantSet) {
                dfsCodeMotion(I, L, DT, Preheader, Visited, InvariantSet);
            }            
        }
        return PreservedAnalyses::all();
    }

    static bool isRequired() { return true; }
};

} // namespace

//-----------------------------------------------------------------------------
// New PM Registration
//-----------------------------------------------------------------------------
llvm::PassPluginLibraryInfo getLoopInvariantPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "LoopInvariantPass", LLVM_VERSION_STRING,
            [](PassBuilder &PB) {
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "loop-invariant-pass") {
                            FPM.addPass(LoopInvariantPass());
                            return true;
                        }
                        return false;
                    });
            }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return getLoopInvariantPassPluginInfo();
}
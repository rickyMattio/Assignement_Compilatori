//=============================================================================
// FILE:
//    LoopFusionPass.cpp
//
// DESCRIPTION:
//    Identifica i loop possibili da fondere. 
//
// USAGE:
//       cd build
//       cmake ..
//       make
//       cd ..
//       clang -O0 -emit-llvm -Xclang -disable-O0-optnone -S TestLoopInvariant.cpp -o test/TestLoopInvariant1.bc
//       opt -passes="mem2reg" test/TestLoopInvariant1.bc -o test/TestLoopInvariant2.bc
//       llvm-dis test/TestLoopInvariant2.bc -o test/TestLoopInvariantOptimized.ll
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
#include "llvm/IR/IRBuilder.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/ADT/SmallString.h"  
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/Metadata.h" 
#include "llvm/ADT/SmallVector.h"
#include <vector> //aggiunto






            //verifichiamo che entrambi abbiano la guadia e che entrambe abbiano la stessa condizione di guardia 
            //preheader del primo domina il preheader del secondo e uìil secondo postdomina il preheade del primo
            //se con guardia faccio il controllo uguale ma con la guardia
            /*controllo l adiacenza
            Se sono adiacenti, chiamo la funzione successiva
            altrimenti vado avanti */



//aggiunta
using namespace llvm;
using namespace std;


namespace {
// Dichiarazione funzione aggiunto
PHINode *getPHI(Loop *L);


void collectLoopsInOrder(LoopInfo &LI, SmallVectorImpl<Loop *> &Loops) {
    SmallVector<Loop *, 8> Queue;

    // Iniziamo dai loop più esterni (top-level)
    for (Loop *TopLevelLoop : LI) {
        Queue.push_back(TopLevelLoop);
    }
    
    std::reverse(Queue.begin(), Queue.end());
    size_t CurrentIdx = 0;
    // Visitiamo prima tutti i fratelli esterni, poi i figli
    while (CurrentIdx < Queue.size()) {
        Loop *L = Queue[CurrentIdx++];
        Loops.push_back(L);
        for(Loop *SubLoop : *L) {
            // Aggiungiamo i figli alla coda per visitarli dopo
            Queue.push_back(SubLoop);
        }
    }
}

void printLoopExitConditions(SmallVectorImpl<Loop *> &Loops, ScalarEvolution &SE) {
    for (Loop *L : Loops) {
        if (!L->getExitingBlock() || !L->getExitBlock()) {
            errs() << "Condizione di uscita: <non determinabile>\n";
            continue;
        }
        const SCEV *ExitCount = SE.getBackedgeTakenCount(L);
        if (isa<SCEVCouldNotCompute>(ExitCount)) {
            errs() << "Condizione di uscita: <non determinabile>\n";
        } else {
            errs() << "Condizione di uscita: ";
            ExitCount->print(errs());
            errs() << "\n";
        }
    }
}



bool checkGuard(BranchInst *branch1, BranchInst *branch2) {
    errs() << "  (CHECK GUARD) Sto controllando le condizioni di guardia...\n";
    if (!branch1 || !branch2 || !branch1->isConditional() || !branch2->isConditional()) {
        errs() << "    Uno o entrambi i branch non sono condizionali o sono nulli. Fallito.\n";
        return false;
    }

    Value *cond1 = branch1->getCondition();
    Value *cond2 = branch2->getCondition();

    Instruction *compInst1 = dyn_cast<Instruction>(cond1);
    Instruction *compInst2 = dyn_cast<Instruction>(cond2);

    if (!compInst1 || !compInst2) {
        errs() << "    Le condizioni non sono istruzioni valide. Fallito.\n";
        return false;
    }

    if (compInst1->isIdenticalTo(compInst2)) {
        errs() << "    Le istruzioni di confronto delle guardie sembrano IDENTICHE. Bene.\n";
        return true;
    }
    errs() << "    Le istruzioni di confronto delle guardie NON sono identiche. Fallito.\n";
    return false;
}

// Funzione per verificare se due loop sono adiacenti per la fusione
bool isAdiacent(Loop *L1, Loop *L2) {
    errs() << "\n--- INIZIO CONTROLLO ADIACENZA TRA I LOOP --- \n";
    // Manteniamo questi parametri per coerenza con la chiamata esterna nel tuo pass,
    // anche se questa specifica logica di adiacenza non li usa direttamente.


    if (!L1 || !L2) {
        errs() << "  Loop nullo rilevato. Non posso controllare l'adiacenza. Uscita.\n";
        return false;
    }

    // Controlla se i loop sono annidati o si sovrappongono in modo inatteso.
    BasicBlock *L1Header = L1->getHeader();
    BasicBlock *L2Header = L2->getHeader();
    if (!L1Header || !L2Header) {
        errs() << "  Loop senza header validi (molto strano). Non adiacenti. Uscita.\n";
        return false;
    }
    // Usiamo il preheader come punto di ingresso per il controllo di contenimento se esiste, altrimenti l'header.
    BasicBlock *L1Entry = L1->getLoopPreheader() ? L1->getLoopPreheader() : L1Header;
    BasicBlock *L2Entry = L2->getLoopPreheader() ? L2->getLoopPreheader() : L2Header;

    if (L1->contains(L2Entry) || L2->contains(L1Entry)) {
        errs() << "  ATTENZIONE: Loop annidati o sovrapposti in modo inatteso. Non adiacenti. Uscita.\n";
        return false;
    }
    errs() << "  Controllato: i loop non sono annidati o sovrapposti.\n";


    // Otteniamo i branch di guardia
    BranchInst *guardBranch1 = L1->getLoopGuardBranch();
    BranchInst *guardBranch2 = L2->getLoopGuardBranch();

    // Caso 1: Entrambi i loop sono "guarded" (hanno un branch di guardia)
    if (guardBranch1 && guardBranch2) {
        errs() << "  Caso: Entrambi i loop sembrano avere una guardia.\n";
        if (!checkGuard(guardBranch1, guardBranch2)) {
            errs() << "    Le condizioni di guardia sono diverse. NON adiacenti. Uscita.\n";
            return false;
        }

        // Verifica se l'uscita del blocco di guardia del primo loop porta direttamente
        // al blocco di guardia del secondo loop.
        BasicBlock *L1GuardBlock = guardBranch1->getParent();
        BasicBlock *L2GuardBlock = guardBranch2->getParent();

        bool foundDirectGuardPath = false;
        for (unsigned i = 0; i < guardBranch1->getNumSuccessors(); ++i) {
            if (guardBranch1->getSuccessor(i) == L2GuardBlock) {
                foundDirectGuardPath = true;
                break;
            }
        }

        if (!foundDirectGuardPath) {
            errs() << "    Il terminatore del blocco guardia del primo loop NON porta direttamente al blocco guardia del secondo. NON adiacenti. Uscita.\n";
            return false;
        }

        // Verifica che la prima istruzione nel blocco di guardia del secondo loop sia la sua condizione.
        // Questo implica che non ci sono istruzioni "intermedie" tra le guardie.
        if (L2GuardBlock->empty() || dyn_cast<Instruction>(L2GuardBlock->begin()) != dyn_cast<Instruction>(guardBranch2->getCondition())) {
            errs() << "    Il blocco di guardia del secondo loop contiene istruzioni prima della sua condizione, o è vuoto. NON adiacenti. Uscita.\n";
            return false;
        }
        
        errs() << "  Entrambi i loop sono guarded, le guardie sono uguali e collegate direttamente. Adiacenti (caso con guardia).\n";
        return true;
    }
    // Caso 2: Nessuno dei due loop è "guarded"
    else if (!guardBranch1 && !guardBranch2) {
        errs() << "  Caso: Nessuno dei due loop ha una guardia esplicita.\n";
        

        BasicBlock *L1ExitBlock = L1->getExitBlock(); // Questo restituisce nullptr se ci sono uscite multiple o nessuna.
        if (!L1ExitBlock) {
            errs() << "    Il primo loop ha uscite multiple o non ha un blocco di uscita unico. NON adiacenti (richiede uscita singola). Uscita.\n";
            return false;
        }

        BasicBlock *L2Preheader = L2->getLoopPreheader();
        if (!L2Preheader) {
             errs() << "    Il secondo loop NON ha un preheader valido. NON adiacenti. Uscita.\n";
             return false;
        }

        // Controlla se il blocco di uscita singolo del primo loop è il preheader del secondo loop.
        if (L1ExitBlock != L2Preheader) {
            errs() << "    Il blocco di uscita del primo loop NON è il preheader del secondo loop. NON adiacenti. Uscita.\n";
            return false;
        }

        // Verifica che la prima istruzione nel blocco che collega i due loop (L1ExitBlock, che è anche L2Preheader)
        // sia un BranchInst. Questo implica che non ci sono istruzioni "intermedie" che fanno calcoli.
        if (L1ExitBlock->empty() || !isa<BranchInst>(L1ExitBlock->begin())) {
            errs() << "    Ci sono istruzioni che non sono branch nel blocco tra i loop. NON adiacenti. Uscita.\n";
            return false;
        }
        
        errs() << "  Nessuna guardia, il primo loop ha uscita singola, collegato direttamente al preheader del secondo senza istruzioni intermedie. Adiacenti.\n";
        return true;
    }
    // Caso 3: Uno è guarded e l'altro no (o viceversa)
    else {
        errs() << "  Caso: Uno dei loop è guarded e l'altro no. NON adiacenti. Uscita.\n";
        return false;
    }
}



// Verifica se esistono variabili scalari (non array) scritte in L1 e lette/usate in L2
bool hasCrossLoopScalarDependence(Loop *L1, Loop *L2) {
    // Raccogli tutte le istruzioni che scrivono (def) nel primo loop
    SmallPtrSet<Value*, 8> defsL1;
    for (BasicBlock *BB : L1->getBlocks()) {
        for (Instruction &I : *BB) {
            // Considera solo le istruzioni che generano un valore (assegnamenti/scalari/promossi a SSA)
            if (!I.getType()->isVoidTy())
                defsL1.insert(&I);
        }
    }

    // Controlla se uno di questi valori è usato (use) nel secondo loop
    for (BasicBlock *BB : L2->getBlocks()) {
        for (Instruction &I : *BB) {
            for (Value *op : I.operands()) {
                if (defsL1.count(op)) {
                    errs() << "  (DATADep) Dipendenza dati scalare trovata tra " << *op
                           << " scritto in L1 e usato in L2: " << I << "\n";
                    return true;
                }
            }
        }
    }
    // Nessuna dipendenza trovata
    return false;
}


PHINode *getPHI(Loop *L) {
    // Scorri tutte le istruzioni dell’header del loop
    for (Instruction &I : *L->getHeader())
        if (auto *PN = dyn_cast<PHINode>(&I))
        return PN;
    return nullptr;
}





bool loopFusion(Loop *l1, Loop *l2, LoopInfo &LI, ScalarEvolution &SE) {
  errs() << "\n--- INIZIO FUSIONE DEI LOOP (FIXED) ---\n";


  PHINode *phi1 = getPHI(l1);
  PHINode *phi2 = getPHI(l2);

  if (!phi1 || !phi2) {
    errs() << "Non trovato phi node - No Fusion\n";
    return false;
  }
  errs() << "PHI L1: " << *phi1 << "\n";
  errs() << "PHI L2: " << *phi2 << "\n";

  // 2. Identificazione dei blocchi chiave dei loop
  // LoopSimplify ensures these are valid.
  BasicBlock *L1Header = l1->getHeader();
  BasicBlock *L1Latch = l1->getLoopLatch();
  BasicBlock *L1ExitingBlock = l1->getExitingBlock();
  BasicBlock *L1ExitTargetBlock = l1->getExitBlock();
  BasicBlock *L1Preheader = l1->getLoopPreheader();
  
  if (!L1Header || !L1Latch || !L1ExitingBlock || !L1ExitTargetBlock || !L1Preheader) {
      errs() << "Niente fusione\n";
      return false;
  }

  BasicBlock *L2HeaderOld = l2->getHeader();
  BasicBlock *L2LatchOld = l2->getLoopLatch();
  BasicBlock *L2PreheaderOld = l2->getLoopPreheader();
  BasicBlock *L2ExitTargetBlock = l2->getExitBlock();
  
  if (!L2HeaderOld || !L2LatchOld || !L2PreheaderOld || !L2ExitTargetBlock) {
      errs() << "Niente fusione.\n";
      return false;
  }

  Value *initVal1 = phi1->getIncomingValueForBlock(L1Preheader);
  Value *initVal2 = phi2->getIncomingValueForBlock(L2PreheaderOld);

  Value *l2_adjusted_iv = phi1;
  ConstantInt *CInit1 = dyn_cast<ConstantInt>(initVal1);
  ConstantInt *CInit2 = dyn_cast<ConstantInt>(initVal2);
  
 
  IRBuilder<> Builder(L1Preheader->getTerminator());

  if (CInit1 && CInit2 && CInit1->getType() == CInit2->getType()) {
    APInt offsetVal = CInit1->getValue() - CInit2->getValue();
    if (!offsetVal.isZero()) {
        l2_adjusted_iv = Builder.CreateAdd(phi1, ConstantInt::get(phi1->getType(), offsetVal), "l2_adj_iv");
        errs() << "DEBUG: " << *l2_adjusted_iv << "\n";
    } else {
        errs() << "DEBUG: \n";
    }
  } else {
      errs() << "DEBUG: \n";
  }

  phi2->replaceAllUsesWith(l2_adjusted_iv);
  errs() << "DEBUG: \n";


  phi2->eraseFromParent(); 
  errs() << "DEBUG: \n";


  for (auto &I : *L2HeaderOld) {
    if (PHINode *PHI = dyn_cast<PHINode>(&I)) {
    
      if (PHI == phi2) continue; 
      
      Value *valueFromPreheader = PHI->getIncomingValueForBlock(L2PreheaderOld);
      
      if (valueFromPreheader) { 
        PHI->removeIncomingValue(L2PreheaderOld);
        PHI->addIncoming(valueFromPreheader, L1ExitingBlock);
        errs() << "DEBUG: " << *PHI << " L2 header dall'uscita di L1.\n";
      } else {
          errs() << "PHI node " << *PHI << " Dal'header di L2 .\n";
      }
    }
  }

  
  auto *L1ExitingTerminator = L1ExitingBlock->getTerminator();
 
  BasicBlock *L1LoopbackTarget = nullptr;
  for (unsigned i = 0; i < L1ExitingTerminator->getNumSuccessors(); ++i) {
    BasicBlock *succ = L1ExitingTerminator->getSuccessor(i);
    if (l1->contains(succ) && succ != L1ExitingBlock) { 
        L1LoopbackTarget = succ;
        break;
    }
  }
  if (!L1LoopbackTarget) {
      errs() << "Non trovato L1 target.\n";
      return false;
  }
  L1ExitingTerminator->replaceSuccessorWith(L1LoopbackTarget, L2HeaderOld);
  errs() << "DEBUG: Redirected L1 loopback " << L1LoopbackTarget->getName() << " a L2 header (" << L2HeaderOld->getName() << ").\n";

  
  auto *L2LatchTerminator = L2LatchOld->getTerminator();
  if (!L2LatchTerminator) {
      errs() << "L2 latch terminator non trovato, fusion impossibile\n";
      return false;
  }
  BasicBlock *L2BackedgeTarget = nullptr; 
  for (unsigned i = 0; i < L2LatchTerminator->getNumSuccessors(); ++i) {
      BasicBlock *succ = L2LatchTerminator->getSuccessor(i);
      if (l2->contains(succ)) { 
          L2BackedgeTarget = succ;
          break;
      }
  }
  if (!L2BackedgeTarget) {
      errs() << "Niente fusione.\n";
      return false;
  }
  L2LatchTerminator->replaceSuccessorWith(L2BackedgeTarget, L1Latch);
  errs() << "DEBUG: Redirected dall'uscita L2 " << L2BackedgeTarget->getName() << " a L1 latch (" << L1Latch->getName() << ").\n";

  
  BasicBlock *L1CurrentExitSucc = nullptr;
  for (unsigned i = 0; i < L1ExitingTerminator->getNumSuccessors(); ++i) {
      BasicBlock *succ = L1ExitingTerminator->getSuccessor(i);
      if (succ == L1ExitTargetBlock) { 
          L1CurrentExitSucc = succ;
          break;
      }
  }
  if (!L1CurrentExitSucc) {
      errs() << "Non trovo il successore dell'uscita di L1. Fusione non possibile.\n";
      return false;
  }
  L1ExitingTerminator->replaceSuccessorWith(L1CurrentExitSucc, L2ExitTargetBlock);
  errs() << "DEBUG: Redirect dall'uscita L1" << L1CurrentExitSucc->getName() << " a L2 latch (" << L2ExitTargetBlock->getName() << ").\n";


  
  SmallVector<BasicBlock*, 8> L2BlocksToMove;
  for (BasicBlock *BB : l2->getBlocks()) {
    if (BB != L2PreheaderOld) { 
      L2BlocksToMove.push_back(BB);
    }
  }
  
  for (BasicBlock *BB : L2BlocksToMove) {
    BB->moveBefore(L1Latch); 
    l1->addBasicBlockToLoop(BB, LI); 
    errs() << "DEBUG: Movimento dal blocco L2 " << BB->getName() << " a L1.\n";
  }

  
  if (!L2PreheaderOld->use_empty()) {
    L2PreheaderOld->replaceAllUsesWith(L1Preheader);
    errs() << "DEBUG: Sovrascritti ultimi usi di L2 in L1.\n";
  }
  L2PreheaderOld->dropAllReferences(); 
  L2PreheaderOld->eraseFromParent();
  errs() << "DEBUG: Rimosso preheader di L2 (" << L2PreheaderOld->getName() << ").\n";

  
  for (auto &I : *L1Latch) {
    if (PHINode *PHI = dyn_cast<PHINode>(&I)) {
     
      Value *existingBackedgeValue = nullptr;
      BasicBlock *existingBackedgeBlock = nullptr;
      for (unsigned i = 0; i < PHI->getNumIncomingValues(); ++i) {
          BasicBlock *incomingBlock = PHI->getIncomingBlock(i);
         
          if (l1->contains(incomingBlock) && incomingBlock != L1Preheader && incomingBlock != L2LatchOld) {
              existingBackedgeValue = PHI->getIncomingValue(i);
              existingBackedgeBlock = incomingBlock;
              break;
          }
      }

     
      bool L2LatchOldAlreadySource = false;
      for (unsigned i = 0; i < PHI->getNumIncomingValues(); ++i) {
          if (PHI->getIncomingBlock(i) == L2LatchOld) {
              L2LatchOldAlreadySource = true;
              break;
          }
      }

      
      if (existingBackedgeValue && !L2LatchOldAlreadySource) {
        PHI->addIncoming(existingBackedgeValue, L2LatchOld);
        errs() << "DEBUG: Aggiungo incoming edge per PHI " << *PHI << " in L1 latch da L2.\n";
      } else if (!existingBackedgeValue) {
          errs() << "WARNING: Non esiste un valore backedge per la PHI" << *PHI << " In L1 latch quindi skippo a L2.\n";
      }
    }
  }

  

  SmallVector<BasicBlock*, 4> NewPreds(predecessors(L1Header).begin(),
                                     predecessors(L1Header).end());

for (auto &I : *L1Header) {
    if (auto *PN = dyn_cast<PHINode>(&I)) {
        // Raccogli le incoming block attuali
        SmallVector<BasicBlock*, 4> ToRemove;
        for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
            BasicBlock *IncomingBB = PN->getIncomingBlock(i);
            // Se il blocco non è più un predessore reale, lo segnalo
            if (!is_contained(NewPreds, IncomingBB)) {
                ToRemove.push_back(IncomingBB);
            }
        }
        // Rimuovo tutte le incoming stale
        for (BasicBlock *BB : ToRemove) {
            PN->removeIncomingValue(BB, /*ErasePHIIfEmpty=*/false);
            errs() << "DEBUG: Rimosso incoming PHI da " << BB->getName() << "\n";
        }
    }
}
  removeUnreachableBlocks(*L1Header->getParent());

  LI.erase(l2);
  errs() << "DEBUG: Rimosso L2 da LoopInfo.\n";

  errs() << "--- FUSIONE DEI LOOP COMPLETATA (FIXED) ---\n";

  // DEBUG: Informazioni sui predecessori di L1Header
errs() << "\n--- DEBUG CFG AFTER FUSION ---\n";
errs() << "Controllo L1Header (" << L1Header->getName() << "):\n";
int L1HeaderPredCount = 0;
for (BasicBlock *Pred : predecessors(L1Header)) {
    errs() << "  Predecessore: " << Pred->getName() << "\n";
    L1HeaderPredCount++;
}
errs() << "Predecessore totale L1Header: " << L1HeaderPredCount << "\n";

if (phi1) {
    errs() << "PHI " << phi1->getName() << " in L1Header Incoming Values:\n";
    errs() << "  Incoming value count: " << phi1->getNumIncomingValues() << "\n";
    for (unsigned i = 0; i < phi1->getNumIncomingValues(); ++i) {
        errs() << "  - Value: " << *phi1->getIncomingValue(i) << " from Block: " << phi1->getIncomingBlock(i)->getName() << "\n";
    }
} else {
    errs() << "Nessun nodo PHI trovato per L1Header(Questo non dovrebbe succedere per un loop canonico).\n";
}
errs() << "------------------------------\n";
  return true;
}

bool areControlFlowEq(Loop *L1, Loop *L2, DominatorTree &DT, PostDominatorTree &PDT) {
    BasicBlock *guardBlock1 = nullptr; // Passa L1 (che è già un puntatore)
    BasicBlock *guardBlock2 = nullptr; // Passa L2 (che è già un puntatore)


    if (L1->isGuarded()) {
        BranchInst *branch1 = L1->getLoopGuardBranch();
        if (branch1) {
            guardBlock1 = branch1->getParent();
            errs() << "    L1 è guarded. Blocco guardia identificato: " << guardBlock1->getName() << "\n";
        } else {
            errs() << "    ATTENZIONE: L1 è marcato come 'isGuarded' ma getLoopGuardBranch() restituisce null. Considero non fusibile.\n";
            return false; 
        }
    } else {
        errs() << "    L1 NON è guarded.\n";
    }

    // Tenta di ottenere il blocco di guardia per il secondo loop
    errs() << "  (CONTROL FLOW) Verifico lo stato di guardia di L2...\n";
    if (L2->isGuarded()) {
        BranchInst *branch2 = L2->getLoopGuardBranch();
        if (branch2) {
            guardBlock2 = branch2->getParent();
            errs() << "    L2 è guarded. Blocco guardia identificato: " << guardBlock2->getName() << "\n";
        } else {
            errs() << "    ATTENZIONE: L2 è marcato come 'isGuarded' ma getLoopGuardBranch() restituisce null. Considero non fusibile.\n";
            return false; 
        }
    } else {
        errs() << "    L2 NON è guarded.\n";
    }




    if(L1->isGuarded() && L2->isGuarded()) {
        if (DT.dominates(guardBlock1, guardBlock2) && 
            PDT.dominates(guardBlock2, guardBlock1)) {
            errs() << "  (CONTROL FLOW) I loop sono equivalenti in termini di controllo di flusso (entrambi guardati).\n";
            return true;
        }
    }else if (!L1->isGuarded() && !L2->isGuarded()) {
        if (DT.dominates(L1->getLoopPreheader(), L2->getLoopPreheader()) && 
            PDT.dominates(L2->getLoopPreheader(), L1->getLoopPreheader())) {
            errs() << "  (CONTROL FLOW) I loop sono equivalenti in termini di controllo di flusso (entrambi non guardati).\n";
            return true;
        }
    }

    return false;
}


bool iterationNumber(Loop *L1, Loop *L2, ScalarEvolution &SE) {
  errs() << "  (ITERAZIONE) Controllo se i loop iterano lo stesso numero di volte.\n";
    
    const SCEV *ExitCount1 = SE.getBackedgeTakenCount(L1);
    const SCEV *ExitCount2 = SE.getBackedgeTakenCount(L2);
    
    if (isa<SCEVCouldNotCompute>(ExitCount1) || isa<SCEVCouldNotCompute>(ExitCount2)) {
        errs() << "  (ITERAZIONE) Non riesco a calcolare le iterazioni per uno dei loop.\n";
        return false;
    }
    
    if (ExitCount1 != ExitCount2) {
        errs() << "  (ITERAZIONE) I loop hanno un numero diverso di iterazioni. L1: ";
        ExitCount1->print(errs());
        errs() << ", L2: ";
        ExitCount2->print(errs());
        errs() << "\n";
        return false;
    }

    errs() << "  (ITERAZIONE) I loop iterano lo stesso numero di volte.\n";
    // Non controlliamo più i valori iniziali qui, perché la fusione
    // può gestire offset, ma richiede modifiche in loopFusion.
    return true;
}


vector<Instruction*> getLoadOrStore(Loop *L, bool isLoad) {
  vector<Instruction*> memInsts;
  // itera su tutti i blocchi del loop
  for (BasicBlock *B : L->getBlocks())
    // itera su tutte le istruzioni del blocco
    for (Instruction &I : *B)
      // se è il tipo giusto (load o store), logga e accumula
      if ((isLoad && isa<LoadInst>(I)) ||
          (!isLoad && isa<StoreInst>(I))) {
        errs() << "\t\tTrovata una" 
               << (isLoad ? "load" : "store") 
               << " istruzione: " << I << "\n";
        memInsts.push_back(&I);
      }
  return memInsts;
}

bool NegativeDistance(Loop *L1, Loop *L2, ScalarEvolution &SE) {
 errs() << "---- INIZIO CONTROLLO DISTANZA ----\n";

  vector<Instruction*> storeInsts[2] = { getLoadOrStore(L1, false), getLoadOrStore(L2, false) };
  vector<Instruction*> loadInsts[2]  = { getLoadOrStore(L2,  true), getLoadOrStore(L1,  true) };
  Loop *storeLoop[2] = { L1, L2 };
  Loop *loadLoop[2]  = { L2, L1 };

  for (int dir = 0; dir < 2; ++dir) {
    // dir == 0: Store L1 -> Load L2
    // dir == 1: Store L2 -> Load L1 (invertito)
    bool invert = (dir == 1);

    for (Instruction *SI : storeInsts[dir]) {
      auto *store = cast<StoreInst>(SI);
      Value *sPtr = store->getPointerOperand();

      // Ottenere l'elemento puntato da GEP. Se non è un GEP, ignora o gestisci con AliasAnalysis
      GetElementPtrInst *gS = dyn_cast<GetElementPtrInst>(sPtr);
      if (!gS) {
        errs() << "\tVado avanti non-GEP puntatore a store:" << *sPtr << "\n";
        continue; // Per semplicità, ignora i puntatori non GEP (richiederebbe AliasAnalysis)
      }
      Value *baseS = gS->getPointerOperand();
      errs() << "\tStore: " << *baseS << "\n";

      for (Instruction *LI : loadInsts[dir]) {
        auto *load = cast<LoadInst>(LI);
        Value *lPtr = load->getPointerOperand();

        GetElementPtrInst *gL = dyn_cast<GetElementPtrInst>(lPtr);
        if (!gL) {
          errs() << "\tVado avanti non-GEP puntatore a load:" << *lPtr << "\n";
          continue; // Per semplicità, ignora
        }
        Value *baseL = gL->getPointerOperand();
        errs() << "\tLoad : " << *baseL << "\n";

        // Se gli accessi sono a array diversi, non c'è dipendenza di distanza
        if (baseS != baseL) {
          errs() << "\tLoad e store array diversi\n";
          continue;
        }

        // Calcola la distanza tra gli accessi
        const SCEV *sS = SE.getSCEVAtScope(sPtr, storeLoop[dir]);
        const SCEV *sL = SE.getSCEVAtScope(lPtr, loadLoop[dir]);

        const SCEV *diff = invert ? SE.getMinusSCEV(sL, sS) : SE.getMinusSCEV(sS, sL);
        errs() << "\tSCEV: " << *diff << "\n";

        // Verifica se la differenza può essere negativa
        if (SE.isKnownNegative(diff)) {
          errs() << "\tDistanza Negativa" << *diff << ")\n";
          return false;
        }
      }
    }
  }

  errs() << "\tFusione possibile non c'è distanza negativa.\n";
  return true;
}




// Pass per identificare i loop possibili da fondere
struct LoopFusionPass : PassInfoMixin<LoopFusionPass> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
        bool atLeastOneFusion = false; // Traccia se è avvenuta almeno una fusione complessiva
        bool Changed = false; // Traccia se è avvenuta una fusione in questa iterazione
        // Il ciclo do-while è cruciale: continuiamo a tentare la fusione finché avvengono cambiamenti
        do {
            Changed = false;
            // Raccogli le informazioni di analisi all'inizio di ogni iterazione del do-while.
            // Questo è FONDAMENTALE perché le modifiche all'IR invalidano le analisi precedenti.
            LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);
            ScalarEvolution &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);
            DominatorTree &DT = FAM.getResult<DominatorTreeAnalysis>(F);
            PostDominatorTree &PDT = FAM.getResult<PostDominatorTreeAnalysis>(F);
            
            SmallVector<Loop *, 8> OrderedLoops;
            // Usa la funzione helper adattata per raccogliere i loop
            collectLoopsInOrder(LI, OrderedLoops);
            
            errs() << "\n--- Inizio analisi per la funzione: " << F.getName() << " ---\n";
            // ci devono essere almeno 2 loop 
            // Se ci sono meno di due loop, non ha senso provare la fusione
            if (OrderedLoops.size() < 2) {
                errs() << "Non ci sono abbastanza loop per tentare la fusione in questa iterazione. Esco.\n";
                break; // Esci dal ciclo do-while
            }
            errs() << "\n=== Iterazione di tentativo fusione dei loop ===\n";
            
            
            
            //printLoopExitConditions(OrderedLoops, SE);
            bool fusionHappened = false; // Traccia se è avvenuta una fusione in questa iterazione
            // Itera attraverso le coppie di loop adiacenti
            for (size_t i = 0; i + 1 < OrderedLoops.size(); ++i) {
                Loop *L1 = OrderedLoops[i];
                Loop *L2 = OrderedLoops[i+1];
                
                // Aggiungi un controllo per i casi in cui i loop potrebbero essere nulli
                if (!L1 || !L2 || !L1->getHeader() || !L2->getHeader()) {
                    errs() << "Skippo il controllo sui loop nulli.\n";
                    continue;
                }

                errs() << "Controllo fusione dei loop " << L1->getHeader()->getName()
                       << " e loop " << L2->getHeader()->getName() << "\n";

                bool canFuse = isAdiacent(L1, L2) && areControlFlowEq(L1, L2, DT, PDT) && iterationNumber(L1, L2, SE) && NegativeDistance(L1, L2, SE) && !hasCrossLoopScalarDependence(L1, L2);

                // Applica tutti i criteri di fusione
                if (canFuse) {
                    
                    errs() << "  TUTTE LE CONDIZIONI DI FUSIONE SONO SODDISFATTE: tentativo di fusione dei loop.\n";
                    
                    // Prova a fondere i loop TOLTO SE
                    if (loopFusion(L1, L2, LI, SE)) {
                        errs() << " Fusione riuscita! Invalido le analisi e riavvio il pass.\n";
                        Changed = true;      // Segnala che è avvenuto un cambiamento in questa iterazione
                        atLeastOneFusion = true; // Segnala che è avvenuto un cambiamento complessivo
                        
                        // Invalida tutte le analisi per questa funzione e rompi il ciclo interno
                        // per riavviare il ciclo do-while con analisi fresche.
                        FAM.invalidate(F, llvm::PreservedAnalyses::none());
                        fusionHappened = true;
                        break; // Esci dal for-loop per ricominciare il do-while
                    } else {
                        errs() << "  Fusione fallita (la stub ha restituito false o si sono verificati problemi nella fusione).\n";
                    }
                } else {
                    errs() << "  I LOOP NON POSSONO ESSERE FUSI a causa di una o più condizioni non soddisfatte.\n";
                }
                errs() << "----------------------------------------\n";
            }
        } while (Changed); // Continua finché almeno una fusione è avvenuta nell'ultima iterazione

        // Se sono avvenute modifiche all'IR, restituisci PreservedAnalyses::none().
        // Altrimenti, se non è successo nulla, restituisci PreservedAnalyses::all().
        return atLeastOneFusion ? PreservedAnalyses::none() : PreservedAnalyses::all();
    }

    static bool isRequired() { return true; }
};

} // namespace

//-----------------------------------------------------------------------------
// New PM Registration
//-----------------------------------------------------------------------------
llvm::PassPluginLibraryInfo getLoopFusionPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "LoopFusionPass", LLVM_VERSION_STRING,
            [](PassBuilder &PB) {
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "loop-fusion-pass") {
                            FPM.addPass(LoopFusionPass());
                            return true;
                        }
                        return false;
                    });
            }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return getLoopFusionPassPluginInfo();
}

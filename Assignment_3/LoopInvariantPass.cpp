//=============================================================================
// FILE:
//    LoopInvariantPass.cpp
//
// DESCRIPTION:
//    Identifica le istruzioni loop invariant in un ciclo. 
//
// USAGE:
//       cd build
//       cmake ..
//       make
//       cd ..
//       clang++ -S -emit-llvm -O0 \
//       -Xclang -disable-O0-optnone \
//       -o test/Test.ll \
//       test/Test_1.cpp
//
//       opt -load-pass-plugin ./build/libLoopInvariantPass.so  \
//       -passes="mem2reg,loop-invariant-pass"    \
//       -S -o test/Test_opt.ll   \
//       test/Test.ll
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

//Funzione di supporto (helper) per raccogliere ricorsivamente tutti i loop, all'interno di un loop dato, 
// in ordine post-order inverso (RPO).
// Questo significa che i loop più interni vengono visitati e salvati per primi.
void collectAllLoopsInRPO(Loop *L, SmallVectorImpl<Loop*> &AllLoops) {
    // Visita prima tutti i sotto-loop (ricorsivamente)
    for (Loop *SubLoop : L->getSubLoops()) {
        collectAllLoopsInRPO(SubLoop, AllLoops);
    }
    // Dopo aver visitato tutti i sotto-loop, aggiunge il loop corrente
    AllLoops.push_back(L);
}

// Controlla se una variabile è "morta" (non usata) fuori dal loop.
// Controlla se tutti gli usi sono interni al loop

bool isDead(Instruction *I, Loop *L) { 
    // Se non è usata, è morta
    if (I->use_empty()) {
        return true;
    }
    
    // Controlla se tutti gli usi (user) sono interni al loop 
    for (User *U : I->users()) {
        if (Instruction *UserInst = dyn_cast<Instruction>(U)) {
            // Se c'è almeno un uso fuori dal loop, non è "morta"
            if (!L->contains(UserInst)) {
                return false;
            }
        }
    }
    
    // Tutti gli usi sono interni al loop, l'istruzione è "morta" fuori dal loop
    return true;
}

// Restituisce true se 'Op' è invariato nel ciclo 'L'; evita controlli ripetuti usando 'CheckedOperands'
bool isOperandLoopInvariant(Value *Op, Loop *L, SmallVectorImpl<Value *> &CheckedOperands); 

// Funzione di supporto per verificare se un'istruzione è loop invariant, cioè se può essere calcolata una volta sola
//  prima del ciclo, e quindi "spostata fuori" dal loop.
// Usa un vettore `CheckedOperands` per evitare controlli duplicati e ricorsioni infinite.

void isLoopInvariant(Instruction &I, Loop *L, SmallVectorImpl<Value *> &CheckedOperands) {
    // Controlla se l'istruzione è già stata verificata come loop-invariant
    if (std::find(CheckedOperands.begin(), CheckedOperands.end(), &I) != CheckedOperands.end()) {
        // Se l'istruzione è già nel vettore, è stata verificata come loop invariant
        return;
    }

    // I nodi PHI nel loop non possono essere loop-invariant
    if (isa<PHINode>(&I)) {
        return;
    }

    // Se l'istruzione ha effetti collaterali (es. scrittura in memoria), non può essere loop invariant
    if (I.mayHaveSideEffects()) {
        return;
    }
    
    // Analizza tutti gli operandi dell'istruzione
    for (Value *Op : I.operands()) {
         // Se un operando è un PHI node nel loop, allora l'istruzione dipende da valori che cambiano 
         // a ogni iterazione non può essere loop-invariant
        if (Instruction *OpInst = dyn_cast<Instruction>(Op)) {
            if (isa<PHINode>(OpInst) && L->contains(OpInst)) {
                return; // Usa un PHI del loop, non può essere loop-invariant
            }
        }

        if (!isOperandLoopInvariant(Op, L, CheckedOperands)) {
            return; // Se anche un solo operando non è loop invariant, esci
        }
    }

    // Se tutti gli operandi sono loop invariant e non ha side effects, allora l'istruzione è loop invariant
    // Aggiungi l'istruzione al vettore CheckedOperands
    CheckedOperands.push_back(&I);
}

// Funzione che determina se un *operando* (Value) è loop-invariant rispetto al loop `L`.
// Chiama `isLoopInvariant` ricorsivamente se l'operando è un'istruzione.
bool isOperandLoopInvariant(Value *Op, Loop *L, SmallVectorImpl<Value *> &CheckedOperands) {
    // Controlla se l'operando è un'istruzione
    if (Instruction *RD = dyn_cast<Instruction>(Op)) {
        // Se è un PHI node nel loop, non è loop-invariant
        if (isa<PHINode>(RD) && L->contains(RD)) {
            return false;
        }

        // Se l'istruzione NON è nel loop, allora è loop invariant
        if (!L->contains(RD)) {
            return true;
        }

        // Se è nel loop, allora verifichiamo ricorsivamente se può essere considerata loop-invariant, solo se anche lei è loop invariant 
        isLoopInvariant(*RD, L, CheckedOperands);
    }

    // Se l'operando NON è un'istruzione (es. costante), è loop invariant per definizione
    return true;
}


// Funzione per verificare se un blocco domina tutte le uscite del loop.
// Non modifica CheckedOperands direttamente.
// Utile per capire se si può spostare un'istruzione fuori dal loop senza modificarne il comportamento.

bool dominatesAllLoopExits(Instruction &I, Loop *L, DominatorTree &DT) {
    // Ottiene il blocco di base (BasicBlock) che contiene l'istruzione `I`
    BasicBlock *BB = I.getParent();

    // Recupera tutti i blocchi di uscita del loop (exit blocks): 
    // sono blocchi che si trovano fuori dal loop ma sono raggiunti dal loop
    SmallVector<BasicBlock *, 8> ExitBlocks;
    L->getExitBlocks(ExitBlocks);

    // Se il loop non ha blocchi di uscita (es. loop infinito), 
    // consideriamo che `I` domina "tutte le uscite" in modo vacuo.
    if (ExitBlocks.empty()) {
        return true; 
    }

    // Verifica, per ciascun blocco di uscita, che `BB` lo domini
    for (BasicBlock *Exit : ExitBlocks) {
        // Se anche un solo blocco di uscita non è dominato, restituiamo false
        if (!DT.dominates(BB, Exit)) {
            return false; // Non domina almeno un blocco di uscita
        }
    }
    return true; // Domina tutti i blocchi di uscita
}

// Funzione che verifica se l'istruzione `I` domina tutti i suoi usi interni al loop `L`.
// Utile nella Loop-Invariant Code Motion (LICM) per sapere se è sicuro spostare `I` fuori dal loop.
// La funzione NON richiede più che gli usi siano in un insieme di istruzioni invarianti.

bool dominatesAllUsesInLoop(Instruction &I, Loop *L, DominatorTree &DT) {
    // Ottiene il blocco in cui l'istruzione è definita
    BasicBlock *DefBlock = I.getParent();
    // Esamina ogni uso dell'istruzione `I`
    for (User *U : I.users()) {
        // Considera solo gli usi che sono anche istruzioni
        if (Instruction *UserInst = dyn_cast<Instruction>(U)) {
            // Se l'uso è nel ciclo allora:
            if (L->contains(UserInst)) {
                // verifica che il blocco di definizione domini il blocco dell'uso
                if (!DT.dominates(DefBlock, UserInst->getParent())) {
                    // Se anche un solo uso nel loop NON è dominato, `I` non può essere spostata
                    return false;
                }
            }
        }
    }
    // Se tutti gli usi nel loop sono dominati da `I`, allora si può potenzialmente spostare
    return true;
}

// Applica Loop-Invariant Code Motion ricorsivamente su `I`.
// Se `I` è invariante e domina i suoi usi, la sposta nel `Preheader`.
// Usa `DT` per la dominanza, `Visited` per evitare ripetizioni, `InvariantSet` per tracciare le invarianti.
void dfsCodeMotion(Instruction *I,
    Loop *L,
    DominatorTree &DT,
    BasicBlock *Preheader,
    SmallPtrSetImpl<Instruction *> &Visited,
    SmallPtrSetImpl<Instruction *> &InvariantSet) {
    if (!Visited.insert(I).second) return; // già visitata

    // Visita in modo ricorsivo le dipendenze
    for (Value *Op : I->operands()) {
        // Se l'operando è un'istruzione ed è nell'insieme degli invarianti
        if (Instruction *OpInst = dyn_cast<Instruction>(Op)) {
            if (InvariantSet.contains(OpInst)) {
                // Continua ricorsivamente con DFS
                dfsCodeMotion(OpInst, L, DT, Preheader, Visited, InvariantSet);
            }
        }
    }

    // Dopo aver visitato le dipendenze, sposta se le condizioni sono rispettate
    if (dominatesAllUsesInLoop(*I, L, DT)) {
        // Non si può spostare un'istruzione terminator (es. br, ret)
        if (I->isTerminator()) {
            errs() << "NON SPOSTATA (terminator): " << *I << "\n";
        } else {
            // Sposta l'istruzione nel preheader del loop, prima della terminazione
            I->moveBefore(Preheader->getTerminator());
            errs() << "Istruzione spostata nel preheader (dfs): " << *I << "\n";
        }
    } else {
        // Se la dominanza non è rispettata, non si può spostare
        errs() << "NON SPOSTATA (domination failure): " << *I << "\n";
    }
}



// Controlla se l'istruzione appare multiple volte nel loop `L`.
// Se sì, non è sicuro spostarla fuori: potrebbe essere ridefinita o interferire.
bool appearsMultipleTimesInLoop(Instruction *I, Loop *L) {
    int count = 0;
    
    // Conta quante volte la stessa variabile viene ridefinita nel loop
    for (BasicBlock *BB : L->blocks()) {
        for (Instruction &Other : *BB) {
            // Confronta se è la stessa istruzione o se assegna alla stessa variabile
            if (&Other == I) {
                count++;
            } else if (Other.getOpcode() == I->getOpcode() && !isa<PHINode>(&Other)) {
                // Per le operazioni aritmetiche, verifica se operano sugli stessi operandi
                // e potrebbero sovrascrivere lo stesso risultato
                bool sameOperands = true;
                if (Other.getNumOperands() == I->getNumOperands()) {
                    for (unsigned i = 0; i < I->getNumOperands(); ++i) {
                        if (Other.getOperand(i) != I->getOperand(i)) {
                            sameOperands = false;
                            break;
                        }
                    }
                    // Se tutti gli operandi coincidono -> conta come "duplicato"
                    if (sameOperands) count++;
                }
            }
        }
    }
    // Se appare più di una volta -> potenziale ambiguità -> non spostare
    return count > 1;
}





// Non spostare MAI istruzioni aritmetiche/logiche se ce ne sono multiple nel loop
// Questa funzione decide se è sicuro spostare un’istruzione fuori dal loop.
// Serve per evitare di fare danni con la LICM spostando cose che potrebbero creare problemi.

bool isSafeToMove(Instruction *I, Loop *L) {
    // Lista di operazioni che sono generalmente sicure da spostare 
    unsigned opcode = I->getOpcode();
    
    switch (opcode) {
        case Instruction::ICmp:      // Confronti tra interi 
        case Instruction::FCmp:      // Confronti floating point/double 
            return true;             // Generalmente sicuri
        
        //Operazioni aritmetiche: possono sembrare innocue, ma se ne troviamo più di una identica dentro al ciclo, meglio non spostarle (troppo rischioso)
        case Instruction::Add:       // Addizioni
        case Instruction::Sub:       // Sottrazioni  
        case Instruction::Mul:       // Moltiplicazioni
        case Instruction::SDiv:      // Divisioni
        case Instruction::UDiv:
            // Per operazioni aritmetiche, controlla se ce ne sono multiple, se ce ne sono evitiamo di spostarle
            return !appearsMultipleTimesInLoop(I, L);
            
        default:
            return false;            
    }
}


// Questo pass identifica e sposta fuori dal ciclo le istruzioni che non cambiano ad ogni iterazione.
// In pratica, se un'istruzione è "loop-invariant", possiamo eseguirla prima che il loop inizi, risparmiando lavoro.
struct LoopInvariantPass : PassInfoMixin<LoopInvariantPass> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
        // Recuperiamo le informazioni sui loop e sull'albero di dominanza della funzione
        LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);
        DominatorTree &DT = FAM.getResult<DominatorTreeAnalysis>(F);

        // Raccogliamo tutti i loop presenti nella funzione (top-level e nidificati) in ordine "inside-out"
        // per permettere agli spostamenti dei loop interni di beneficiare quelli esterni.
        SmallVector<Loop*> AllLoopsInOrder;
        for (Loop *TopLevelL : LI) {
            // Usa la funzione helper per popolare AllLoopsInOrder con il TopLevelL
            // e tutti i suoi sub-loop in reverse post-order.
            collectAllLoopsInRPO(TopLevelL, AllLoopsInOrder);
        }
        
        // Ordiniamo i loop in base alla profondità: prima quelli più interni, poi quelli esterni
        std::sort(AllLoopsInOrder.begin(), AllLoopsInOrder.end(),
          [](const Loop *A, const Loop *B) {
              return A->getLoopDepth() > B->getLoopDepth(); // Ordina dal più profondo al meno profondo
          });


        // itera su tutti i loop nell'ordine desiderato (gestione loop nidificati).
        for (Loop *L : AllLoopsInOrder) {
            // Stampiamo il nome del blocco di header (giusto per debug e chiarezza)
            if (BasicBlock *Header = L->getHeader()) {
                errs() << "Analizzando il ciclo: " << Header->getName() << " (Profondità: " << L->getLoopDepth() << ")\n";
            } else {
                errs() << "Ciclo senza header trovato.\n";
                continue;
            }
  
            // Verifica se il loop ha un preheader (blocco che precede il loop), necessario per il code motion
            // È lì che sposteremo le istruzioni valide.
            BasicBlock *Preheader = L->getLoopPreheader();
            if (!Preheader) {
                errs() << "Nessun preheader trovato per il ciclo, impossibile spostare istruzioni. Skipping.\n";
                continue;
            }

            //  Identificazione delle istruzioni Loop-Invariant 
            // CheckedOperands conterrà tutte le istruzioni identificate come loop-invariant.
            SmallVector<Value *, 8> LoopInvariantInstructions;
            // Itera su tutti i blocchi del ciclo
            for (BasicBlock *BB : L->blocks()) {
                // Itera su tutte le istruzioni nel blocco
                for (Instruction &I : *BB) {
                    // Esegui isLoopInvariant per aggiornare LoopInvariantInstructions
                    isLoopInvariant(I, L, LoopInvariantInstructions); // La funzione riempie il vettore 
                }
            }

            if (LoopInvariantInstructions.empty()) { // Usiamo il nuovo nome della variabile
                errs() << "Nessuna istruzione loop-invariant trovata per questo ciclo. Skipping.\n";
                continue;
            }

            errs() << "\nIdentificate istruzioni loop-invariant per " << (L->getHeader() ? L->getHeader()->getName() : "ciclo") << ":\n";
            for (Value *Val : LoopInvariantInstructions) {
                errs() << "\t" << *Val << "\n";
            }
            errs() << "...\n";

            // Filtro dei candidati per il Code Motion
            // Costruisci l'insieme delle istruzioni finali candidabili al movimento.
            SmallPtrSet<Instruction *, 8> FinalCandidatesForMotion;
            

            // Itera sulle istruzioni identificate come loop-invariant e applica le condizioni aggiuntive per lo spostamento.
            for (Value *Val : LoopInvariantInstructions) {
                if (Instruction *I = dyn_cast<Instruction>(Val)) {
                    
                    // Non considerare istruzioni terminator
                    if (I->isTerminator()) {
                        continue;
                    }

                     //Verifica se è sicuro spostare l'istruzione
                    if (!isSafeToMove(I, L)) {
                        errs() << "Istruzione non candidata (non sicura da spostare): " << *I << "\n";
                        continue;
                    }
                    


                    //  Dominanza delle uscite OPPURE liveness esterna
                    bool DominatesExits = dominatesAllLoopExits(*I, L, DT);
                    bool IsDeadOutside = isDead(I, L);

                    if (!DominatesExits && !IsDeadOutside) {
                        errs() << "Istruzione non candidata (non domina uscite E non è morta fuori dal loop): " << *I << "\n";
                        continue;
                    }

                    // L'istruzione deve dominare tutti i suoi usi interni al loop.
                    if (!dominatesAllUsesInLoop(*I, L, DT)) {
                        errs() << "Istruzione non candidata (non domina tutti gli usi interni): " << *I << "\n";
                        continue;
                    }

                    // Se tutte le condizioni sono soddisfatte, aggiungi come candidato finale.
                    FinalCandidatesForMotion.insert(I);
                }
            }


            if (FinalCandidatesForMotion.empty()) {
                errs() << "Nessun candidato valido per code motion per questo ciclo. Skipping.\n";
                continue;
            }

            errs() << "\nCandidati finali per code motion in " << (L->getHeader() ? L->getHeader()->getName() : "ciclo") << ":\n";
            for (Instruction *I : FinalCandidatesForMotion) {
                errs() << "\t" << *I << "\n";
            }
            errs() << "...\n";

            // DFS bottom-up che sposta direttamente nel preheader
            // Questo per assicurarci di spostare prima le dipendenze (tipo a = ..., b = a + 1)
            SmallPtrSet<Instruction *, 8> Visited;

            // dfsCodeMotion ora usa FinalCandidatesForMotion per il parametro InvariantSet, assicurando che le dipendenze siano controllate solo tra i candidati validi.
            for (Instruction *I : FinalCandidatesForMotion) {
                dfsCodeMotion(I, L, DT, Preheader, Visited, FinalCandidatesForMotion);
            }            
            errs() << "\nCode motion completato per il ciclo " << (L->getHeader() ? L->getHeader()->getName() : "senza header") << ".\n";
        }
        return PreservedAnalyses::all(); // Il pass non invalida nulla in modo specifico
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
                // Registriamo il nostro pass personalizzato, dandogli un nome che può essere usato da terminale
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        // Se da terminale viene passato "loop-invariant-pass"
                        if (Name == "loop-invariant-pass") {
                            // Aggiungiamo il nostro pass alla pipeline
                            FPM.addPass(LoopInvariantPass());
                            return true;
                        }
                        return false; // Altrimenti, ignoriamo
                    });
            }};
}

// Funzione di ingresso richiesta da LLVM per tutti i plugin dinamici.
// Viene chiamata automaticamente da `opt` quando carichiamo il plugin con -load-pass-plugin
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return getLoopInvariantPassPluginInfo();
}

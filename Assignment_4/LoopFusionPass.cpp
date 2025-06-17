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
//       clang -O0 -emit-llvm -c test/TestLoop2.cpp -o test/testLoopFusion.bc
//
//       opt   -load-pass-plugin=./build/libLoopFusion.so   -passes="mem2reg,loop-simplify,loop-fusion-opt"   test/testLoopFusion.bc   -o test/testLoopFusion_opt.bc 
//
//       llvm-dis test/testLoopFusion_opt.bc -o test/testLoopFusion_opt.ll
//
// License: MIT
//=============================================================================


#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Constants.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Analysis/AliasAnalysis.h"


using namespace llvm;


namespace {

    struct LoopFusionPass : PassInfoMixin<LoopFusionPass> {

        // Funzione per cancellare completamente un blocco base (BasicBlock)
        // Prima cancella tutte le istruzioni al suo interno (una per una),
        // poi rimuove anche il blocco stesso dal grafo di controllo.
        void deleteBlock(BasicBlock* BB){
            // Finché ci sono istruzioni dentro il blocco
            while (!BB->empty()) {
                Instruction &I = BB->back(); // Prendi l'ultima istruzione
                I.eraseFromParent(); // Eliminala dal blocco
            }

            // Alla fine elimina anche il blocco stesso
            BB->eraseFromParent();
        }

        /// Restituisce true se PHI è un LCSSA PHI per il loop L :
        /// - non si trova all’interno del loop L
        /// - ciascuno dei suoi blocchi di ingresso proviene da dentro L.
        bool isLCSSAPhi(PHINode *PHI, Loop *L) {
            // Se il PHI è già dentro L, non è LCSSA
            if (L->contains(PHI->getParent()))
                return false;

            // Controlla che tutte le incoming block provengano da dentro L
            for (unsigned i = 0, e = PHI->getNumIncomingValues(); i < e; ++i) {
                if (!L->contains(PHI->getIncomingBlock(i)))
                    return false;
            }

            // Passato tutti i controlli, è un LCSSA PHI
            return true;
        }

        // Funzione principale che si occupa di fondere due loop consecutivi
        Loop* LoopFusion(Function &F, FunctionAnalysisManager &FAM, Loop *first, Loop *second) {
            // Recupera le analisi necessarie per scalar evolution e loop info
            ScalarEvolution &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);
            auto &LI = FAM.getResult<LoopAnalysis>(F);

            // Recupero componenti fondamentali del primo loop 
            auto *firstPreheader = first->getLoopPreheader();   // blocco d’ingresso del primo loop
            auto *firstLatch = first->getLoopLatch();       // blocco di latch del primo loop
            auto *firstBody = firstLatch->getSinglePredecessor(); // corpo del primo loop
            auto *firstHeader = firstPreheader->getSingleSuccessor(); // header del primo loop
            auto *firstGuard = first->getLoopGuardBranch(); // eventuale guardia condizionale
            auto *firstExit = first->getExitBlock();      // blocco di uscita del primo loop

            // Recupero componenti fondamentali del secondo loop 
            auto *secondPreheader = second->getLoopPreheader(); // idem per il secondo loop
            auto *secondLatch = second->getLoopLatch();
            auto *secondBody = secondLatch->getSinglePredecessor();
            auto *secondHeader = secondPreheader->getSingleSuccessor();
            auto *secondExit = second->getExitBlock();
            auto *secondGuard = second->getLoopGuardBranch();

            // Sostituisco la variabile di induzione del secondo con quella del primo 
            auto *firstIV = first->getInductionVariable(SE);
            auto *secondIV = second->getInductionVariable(SE);
            secondIV->replaceAllUsesWith(firstIV); // tutte le occorrenze di secondIV diventano firstIV
            secondIV->eraseFromParent();           // elimino il phi node inutilizzato (della seconda variabile)

            

            // Redirect dei valori in ingresso all’header del secondo loop
            secondHeader->replacePhiUsesWith(secondLatch, firstLatch);
            secondHeader->replacePhiUsesWith(secondPreheader, firstPreheader);
            secondPreheader->replacePhiUsesWith(secondPreheader->getSinglePredecessor(), firstBody);
            secondExit->replacePhiUsesWith(secondLatch, firstLatch);

            // Raccogliamo tutti i PHI nel secondo header
            SmallVector<PHINode *, 8> secondHeaderPHIs;
            for (auto &I : *secondHeader){
                if (auto *PHI = dyn_cast<PHINode>(&I)){
                    secondHeaderPHIs.push_back(PHI);
                }
            }
            // E quelli nel primo header, per aggiornare i loro incoming values
            SmallVector<PHINode *, 8> firstHeaderPHIs;
            for (auto &I : *firstHeader){
                if (auto *PHI = dyn_cast<PHINode>(&I)){
                    firstHeaderPHIs.push_back(PHI);
                }
            
            }

            // Individua il primo punto nel header a partire dal quale è presente un'istruzione non-PHI,
            // utile per sapere dove inserire codice non correlato alle PHI
            Instruction *insertBefore = firstHeader->getFirstNonPHI();

            // Spostiamo o eliminiamo i PHI del secondo header
            for (auto *PHI : secondHeaderPHIs) {
                Value *in0 = PHI->getIncomingValue(0);
                Value *in1 = PHI->getIncomingValue(1);

                // Se è un LCSSA PHI collegato all’uscita del primo loop
                if (auto *lcssaPHI = dyn_cast<PHINode>(in0)) {
                    if (firstExit == lcssaPHI->getParent() && isLCSSAPhi(lcssaPHI, first)) {
                        // Cerca nei PHI del primo header se uno punta allo stesso valore del LCSSA (collegamento tra firstHeaderPHIs e lcssaPHI)
                        Value *lcssaValue = lcssaPHI->getIncomingValue(0);
                        for (auto *fPHI : firstHeaderPHIs) {
                            if (fPHI->getIncomingValue(1) == lcssaValue){
                                fPHI->setIncomingValue(1, in1); //aggiorna valore 
                            }
                        }
                        PHI->replaceAllUsesWith(lcssaValue);
                        PHI->eraseFromParent();
                        lcssaPHI->eraseFromParent();
                        continue;
                    }
                }
                // Se non è un caso LCSSA speciale, spostalo semplicemente nel primo header
                PHI->moveBefore(insertBefore);
            }

            // Spostamento dei PHI LCSSA da firstExit a secondExit 
            Instruction *movePoint = secondExit->getFirstNonPHI(); // Trova il punto prima del quale inserire i PHI spostati
            SmallVector<PHINode *> lcssaToMove; // Vettore per raccogliere i PHI da spostare
            for (Instruction &I : *firstExit) {
                if (auto *phi = dyn_cast<PHINode>(&I)) {
                    // Aggiorno il block di incoming per mantenere correttezza
                    phi->setIncomingBlock(0, firstLatch); 
                    lcssaToMove.push_back(phi); // Salva il PHI per spostarlo dopo
                }
            }
            for (auto *phi : lcssaToMove){
                phi->moveBefore(movePoint); // Sposta il PHI in secondExit
            }
            //  Gestione delle guardie (se esistono) 
            if (firstGuard && secondGuard) {
                // Ricavo la destinazione comune dopo il secondo exit 
                BasicBlock *guardDest = secondExit->getSingleSuccessor();

                // Reindirizzo il ramo di guardia del primo loop
                firstGuard->setSuccessor(1, guardDest); // Cambia il ramo "vero" del primo guardia per puntare a guardDest
                guardDest->replacePhiUsesWith(secondGuard->getParent(), firstGuard->getParent()); // Aggiorna i PHI in guardDest

                // Aggiorno anche il secondo guard branch
                secondGuard->replaceSuccessorWith(guardDest, secondGuard->getParent()); // Cambia la destinazione del secondo guardia
                firstExit->getTerminator()->setSuccessor(0, firstExit); // Sistemo il terminatore del primo exit (transitorio)

                // Sposto le istruzioni dal secondo guard-block al guardDest
                Instruction *insertPt = guardDest->getFirstNonPHI(); // Trova dove inserire le istruzioni spostate dal secondo guard-block
                SmallVector<Instruction *> toMove; // Raccolta delle istruzioni da spostare
                for (Instruction &I : *secondGuard->getParent()) {
                    if (!I.isTerminator() && &I != secondGuard->getCondition()){
                        toMove.push_back(&I); // Prende tutte le istruzioni tranne la condizione e il terminatore
                    }
                }
                for (auto *inst : toMove){
                    inst->moveBefore(insertPt); // Sposta le istruzioni utili dentro guardDest
                }
                // Aggiusto i PHI su guardDest e pulisco i blocchi
                guardDest->replacePhiUsesWith(firstExit, secondExit); // Aggiorna i PHI in guardDest per usare il nuovo predecessore
                deleteBlock(secondGuard->getParent()); // Elimina il blocco del secondo guardia
                deleteBlock(firstExit); // Elimina il blocco di uscita del primo loop
            }

            // Ripristina il controllo di flusso 
            // Il latch del primo ora punta al secondExit
            firstLatch->getTerminator()->setSuccessor(1, secondExit); 
            firstBody->getTerminator()->replaceSuccessorWith(firstLatch, secondHeader); // Rimpiazza il salto verso firstLatch con secondHeader
            secondBody->getTerminator()->replaceSuccessorWith(secondLatch, firstLatch); // SecondBody ora salta direttamente su firstLatch
            secondLatch->getTerminator()->replaceSuccessorWith(secondExit, secondLatch); // SecondLatch salta su sé stesso (in attesa di eliminazione)

            // Rimuovo i blocchi di preheader e latch del secondo loop
            deleteBlock(secondLatch); // Elimina il latch del secondo loop (non più utile)
            deleteBlock(secondPreheader); // Elimina il preheader del secondo loop

            // Integrazione dei blocchi del secondo loop nel primo 
            SmallVector<BasicBlock *> secondBlocks; // Blocchi da aggiungere al primo loop
            secondBlocks.push_back(secondHeader); // Aggiungiamo almeno l'header del secondo loop
            for (auto *bb : secondBlocks) {
                first->addBasicBlockToLoop(bb, LI); // Registra il blocco come parte del primo loop
                bb->moveBefore(firstLatch); // Lo sposta fisicamente prima del latch del primo loop
            }

            // Restituisco il loop fuso (ora contenente anche il secondo)
            return first;
        }


        // Verifica se i due loop sono adiacenti 
        // Restituisce true se l’uscita di `first` coincide con l’ingresso di `second`,
        // tenendo conto del fatto che entrambi i loop possono essere “guarded”.
        bool areLoopsAdiacent(Loop *first, Loop *second){
            
            // Recupera il blocco di uscita di first 
            BasicBlock *ExitBBfirst = nullptr;
            if(first->isGuarded()) {
                // Se guarded, l’ExitBlock() punta al guard; il successore è il vero punto di uscita
                ExitBBfirst = first->getExitBlock()->getSingleSuccessor();
            } else {
                // Altrimenti, l’ExitBlock è già il blocco di uscita
                ExitBBfirst = first->getExitBlock();
            }
            if(!ExitBBfirst) return false;
            // Determina il blocco di ingresso di second
            BasicBlock *EntryBBsecond = nullptr;
            if(second->isGuarded()) {
                // Se guarded, l’ingresso è il blocco del guard
                EntryBBsecond = second->getLoopGuardBranch()->getParent();
            } else {
                // Altrimenti, l’ingresso è il preheader
                EntryBBsecond = second->getLoopPreheader();
            }
            // Se anche qui non troviamo nulla, non possiamo procedere
            if(!EntryBBsecond) return false;

            // Stampa se entrambi i loop sono guarded o meno
            errs() << "   [Adj-detail] first->isGuarded()=" << first->isGuarded()
            << ", second->isGuarded()=" << second->isGuarded() << "\n";

            // 3) I loop sono adiacenti se l’uscita di first è esattamente l’ingresso di second
            return  ExitBBfirst == EntryBBsecond;
        }


        // Restituisce true se i due loop hanno lo stesso trip count (Numero di iterazioni) calcolabile da ScalarEvolution.
        bool iteractionsNumber(Function &F,FunctionAnalysisManager &FAM, Loop *first, Loop *second) {
            // Recuperiamo l’analisi di ScalarEvolution per la funzione F
            auto &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);

            // Calcoliamo il numero di iterazioni (backedge-taken count) per entrambi i loop
            const SCEV *firstTripCount = SE.getBackedgeTakenCount(first);
            // Stessa cosa per il secondo loop
            const SCEV *secondTripCount = SE.getBackedgeTakenCount(second);

            // Se non è stato possibile calcolare il trip count di uno dei due, ritorna false
            if (isa<SCEVCouldNotCompute>(firstTripCount) || isa<SCEVCouldNotCompute>(secondTripCount)) {
                // Stampiamo un messaggio per capire quale dei due è fallito (se uno dei 2 lo ha fatto)
                errs() << "   [TripCount] CouldNotCompute for "
            << (isa<SCEVCouldNotCompute>(firstTripCount) ? "first":"second")
            << "\n";
                // E usciamo con false: non possiamo sapere se sono uguali
                return false;
            }

            // Altrimenti stampiamo i due trip count per confronto visivo (debug)
            errs() << "   [TripCount] first = " << *firstTripCount<< ", second = " << *secondTripCount << "\n";

            // E ritorniamo true solo se i due valori sono esattamente identici 
            return firstTripCount == secondTripCount;
        }



        // Restituisce true se le strutture di controllo dei loop `first` e `second` sono equivalenti,
        // cioè se i loro header (o guard-block, se presenti)  si dominano a vicenda, 
        // e se i relativi branch di guardia (se condizionali) sono identici.
        bool sameCF(Function &F, FunctionAnalysisManager &FAM, Loop *first, Loop *second) {
            // Prendiamo le analisi di dominanza e post-dominanza
            DominatorTree &DT  = FAM.getResult<DominatorTreeAnalysis>(F);
            PostDominatorTree &PDT = FAM.getResult<PostDominatorTreeAnalysis>(F);

            // Header di default dei due loop
            BasicBlock *firstHeader = first->getHeader();
            BasicBlock *secondHeader = second->getHeader();

            // Se entrambi i loop hanno un guard, usa il blocco del branch di guardia
            bool bothGuarded = true;
            // Se entrambi i loop hanno un blocco di guardia (cioè sono guarded)
            if (first->isGuarded() && second->isGuarded()) {
                // Prendiamo i branch di guardia dei due loop
                BranchInst *firstGuard = first->getLoopGuardBranch();
                BranchInst *secondGuard = second->getLoopGuardBranch();
                // Aggiorniamo gli header usiamo i blocchi che contengono il branch
                firstHeader = firstGuard->getParent();
                secondHeader = secondGuard->getParent();

                // Se sono entrambi branch condizionali su un ICMP, verifica che il confronto sia identico
                if (firstGuard->isConditional() && secondGuard->isConditional()) {
                    if (auto *firstICMP = dyn_cast<ICmpInst>(firstGuard->getCondition())) {
                        if (auto *secondICMP = dyn_cast<ICmpInst>(secondGuard->getCondition())) {
                            bothGuarded = firstICMP->isIdenticalTo(secondICMP);
                        }
                    }
                }
            }
            // Stampiamo un messaggio di debug con i nomi dei blocchi coinvolti
            errs() << "   [CF] firstHeader = " << firstHeader->getName() 
            << ", secondHeader = " << secondHeader->getName()
            << ", bothGuarded = " << bothGuarded << "\n";

            // I loop sono “equivalenti” nel controllo se i due header si dominano a vicenda
            // e, in caso di guardie, le condizioni di guardia sono identiche
            return DT.dominates(firstHeader, secondHeader) && PDT.dominates(secondHeader, firstHeader) && bothGuarded;
        }



        // Se l’istruzione è un LoadInst o StoreInst, restituisce il puntatore di memoria
        // altrimenti ritorna nullptr.
        static Value *loadOrStore(Instruction *I) {
            // Se è uno store, ritorna l'operando su cui memorizza il valore (il puntatore)
            if (auto *StoreInstruction = dyn_cast<StoreInst>(I))
                return StoreInstruction->getPointerOperand();
            // Se è un load, ritorna il puntatore da cui legge
            if (auto *LoadInstruction = dyn_cast<LoadInst>(I))
                return LoadInstruction->getPointerOperand();
            // In tutti gli altri casi, non ci interessa
            return nullptr;
        }

        // Controlla che non ci siano dipendenze di memoria “pericolose” tra first e second.
        // Restituisce false se trova una dipendenza con offset negativo, true altrimenti.
        static bool noDependencies(Function &F, FunctionAnalysisManager &FAM, Loop *first, Loop *second) {
            errs() << "   [Deps] Controllo dipendenze tra i due loop...\n";
            // Recupera l’analisi delle dipendenze tra istruzioni
            auto &DI = FAM.getResult<DependenceAnalysis>(F);
            // Recupera ScalarEvolution per l’analisi degli accessi a memoria nel tempo
            auto &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);
            // Serve per sapere la dimensione in memoria dei tipi
            const DataLayout &DL = F.getParent()->getDataLayout();

            // Per ogni istruzione in first
            for (BasicBlock *BB : first->getBlocks()) {
                for (Instruction &I : *BB) {
                // e per ogni istruzione in second
                    for (BasicBlock *BB2 : second->getBlocks()) {
                        for (Instruction &I2 : *BB2) {
                        // verifica se c’è una dipendenza obbligatoria (must follow)
                            if (auto Dep = DI.depends(&I, &I2, /*MustFollow*/ true)) {
                                errs() << "Dipendenza tra: " << I << " e " << I2 << "\n";
                                // Prendi i puntatori di memoria (solo Load/Store) 
                                Value *Ptr1 = loadOrStore(&I);
                                Value *Ptr2 = loadOrStore(&I2);
                                // Se uno dei due non è load/store, salta
                                if (!Ptr1 || !Ptr2)
                                continue;

                                // Rappresentali come SCEV 
                                const SCEV *Expression1 = SE.getSCEV(Ptr1);
                                const SCEV *Expression2 = SE.getSCEV(Ptr2);
                                // SCEVAddRecExpr >> tipo di oggetto che rappresenta come cambia una certa variabile ad ogni giro di loop
                                auto *AddRec1 = dyn_cast<SCEVAddRecExpr>(Expression1);
                                auto *AddRec2 = dyn_cast<SCEVAddRecExpr>(Expression2);

                                // Se hanno lo stesso passo di ricorrenza e sono entrambe AddRec
                                if (AddRec1 && AddRec2) {
                                    if(AddRec1->getStepRecurrence(SE) == AddRec2->getStepRecurrence(SE)){
                                        // Calcola la differenza degli offset iniziali dei due accessi
                                        const SCEV *DistStart = SE.getMinusSCEV(AddRec1->getStart(), AddRec2->getStart());
                                        // Se la differenza è una costante, possiamo interpretarla
                                        if (auto *ConstDist = dyn_cast<SCEVConstant>(DistStart)) {
                                            int64_t ByteOffset = ConstDist->getAPInt().getSExtValue();
                                            // Trasforma in offset di elementi 
                                            Type *ElemType = nullptr;
                                            if (auto *GEP1 = dyn_cast<GetElementPtrInst>(Ptr1)) {
                                                // Se Ptr1 è un GEP, ne prendo il tipo di risultato
                                                ElemType = GEP1->getResultElementType();
                                            } else {
                                                // Altrimenti assumo che Ptr2 sia un GEP (cast sicuro)
                                                auto *GEP2 = cast<GetElementPtrInst>(Ptr2);
                                                ElemType = GEP2->getResultElementType();
                                            }
                                            // unsigned int in byte mentre int è senza unsigned
                                            uint64_t ElemSize = DL.getTypeAllocSize(ElemType);
                                            // Calcoliamo la distanza in elementi (non in byte)
                                            int64_t ElemOffset = ByteOffset / static_cast<int64_t>(ElemSize);
                                            errs() << "Distanza in elementi: " << ElemOffset << "\n";
                                            // Se l'offset è negativo, non è sicuro
                                            if (ElemOffset < 0)
                                                return false;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Nessuna dipendenza pericolosa trovata
            return true;
        }


        bool Optimization(Function &F, FunctionAnalysisManager &FAM, Loop *first, Loop *second){
             // Verifica se i due loop sono adiacenti (cioè se il primo finisce dove il secondo inizia)
            bool adj = areLoopsAdiacent(first, second);
            errs() << "   [Opt] areLoopsAdiacent = " << adj << "\n";
            // Verifica se i due loop hanno lo stesso numero di iterazioni
            bool trip = iteractionsNumber(F, FAM, first, second);
            errs() << "   [Opt] iteractionsNumber = " << trip << "\n";
            // Verifica se le strutture di controllo dei due loop (branch e dominanza) sono compatibili
            bool cf = sameCF(F, FAM, first, second);
            errs() << "   [Opt] sameCF = " << cf << "\n";
            // Verifica se non ci sono dipendenze di memoria che impediscono la fusione
            bool dep = noDependencies(F, FAM, first, second);
            errs() << "   [Opt] noDependencies = " << dep << "\n";
            // I loop sono ottimizzabili solo se tutte le condizioni sopra sono vere
            return adj && trip && cf && dep;
        }

        
        // Applichiamo il pass su ogni BasicBlock della funzione
        bool runOnFunction(Function &F, FunctionAnalysisManager &FAM) {
            // Messaggio di inizio: stiamo analizzando la funzione F
            errs() << "=== LoopFusionPass su funzione: " << F.getName() << " ===\n";
            
            // Recupera le informazioni sui loop presenti nella funzione
            LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);
            // Indica se è stata fatta almeno una modifica
            bool Changed = false;
            // Otteniamo i loop più esterni (top-level) e li mettiamo in ordine
            SmallVector<Loop*,8> OrderedLoops(LI.getTopLevelLoops().begin(),LI.getTopLevelLoops().end());

            // Variabile per salvare il loop "precedente" nel confronto
            Loop* FirstLoop = nullptr;
            // Scorriamo i loop in ordine inverso (dal più interno al più esterno)
            for (auto L = OrderedLoops.rbegin(); L != OrderedLoops.rend(); ++L) {                
                Loop* SecondLoop = *L;
                
                // Stampa quali loop stiamo considerando per la fusione
                errs() << "   > Verifico ottimizzazione tra FirstLoop: "
                << (FirstLoop ? FirstLoop->getHeader()->getName() : "null")
                << " e SecondLoop: "
                << SecondLoop->getHeader()->getName() << "\n";

                // Se abbiamo un FirstLoop e la coppia è ottimizzabile
                if (FirstLoop && Optimization(F, FAM, FirstLoop, SecondLoop)) {
                    // i loop possono essere fusi
                    errs() << "\noptimizable loops: " << FirstLoop << " " << SecondLoop << "\n";
                    errs() << "   >> Loop fusi: "
                    << FirstLoop->getHeader()->getName()
                     << " + " << SecondLoop->getHeader()->getName() << "\n";

                    // Fonde i due loop e aggiorna il FirstLoop al loop fuso
                    FirstLoop = LoopFusion(F, FAM, FirstLoop, SecondLoop);
                    Changed = true; // Segna che è stata fatta una modifica
                } else {
                    // Altrimenti, non è stato possibile fondere: aggiorna FirstLoop
                    errs() << "   >> Non fuso, o per mancata ottimizzazione o è il primo loop\n";
                    FirstLoop = SecondLoop;
                }
            }


            errs() << "=== Fine LoopFusionPass, Changed = " << Changed << " ===\n";
            return Changed; // Ritorna true se c'è stata almeno una modifica
        }

    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
         // Se la funzione è stata modificata, dichiara che nessuna analisi è stata preservata
        if (runOnFunction(F, FAM)) { return PreservedAnalyses::none(); }
        return PreservedAnalyses::all(); // Altrimenti, segnala che tutte le analisi sono ancora valide
    }

        static bool isRequired() { return true; }
    };




}




//-----------------------------------------------------------------------------
// New PM Registration
//-----------------------------------------------------------------------------

llvm::PassPluginLibraryInfo getLoopFusionPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "LoopFusionPass", LLVM_VERSION_STRING,
            // qui diciamo a LLVM cosa fare se trova "loop-fusion-pass"
            [](PassBuilder &PB) {
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        // Se il nome del pass richiesto è "loop-fusion-pass"
                        if (Name == "loop-fusion-pass") {
                            // Aggiungiamo il nostro pass personalizzato al FunctionPassManager
                            FPM.addPass(LoopFusionPass());
                            return true; // Segnala che la registrazione ha avuto successo
                        }
                        // Se il nome non corrisponde, non facciamo nulla
                        return false;
                    });
            }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
     // Chiama semplicemente la funzione che abbiamo definito sopra
    return getLoopFusionPassPluginInfo();
}

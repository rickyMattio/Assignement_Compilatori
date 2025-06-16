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

// Eliminata la funzione isSpecificInt, poiché non è più necessaria, poichè il controllo se uno tra Op0 e Op1
// viene fatto direttamente nella funzione AlgebraicIdentity(se uno dei è una variabile = 0 o a 1)
/*
 *   bool isSpecificInt(Value *V, int64_t val) {
 *       if (ConstantInt *C = dyn_cast<ConstantInt>(V)) {
 *           return C->getValue().getSExtValue() == val;
 *       }
 *       return false;
 *   }
 */


// Algebraic Identity
// Questo pass cerca di semplificare alcune operazioni banali all'interno dei blocchi di codice,
// come ad esempio x + 0, x * 1, x / 1... che possono essere sostituite direttamente con x.

struct AlgebraicIdentity : PassInfoMixin<AlgebraicIdentity> {
    // Funzione principale del pass: analizza un BasicBlock e semplifica alcune operazioni binarie
    bool runOnBasicBlock(BasicBlock &B) {
        
        bool Changed = false; // Tiene traccia se è stato modificato qualcosa 
         
        std::vector<Instruction*> InstructionsToRemove; // Qui salviamo le istruzioni da eliminare dopo l'analisi
        
        // Scorriamo tutte le istruzioni presenti nel BasicBlock
        for(Instruction &Inst : B){
            Instruction *I = &Inst;
             // Verifichiamo che l'istruzione sia un operatore binario  come add, sub, mul, sdiv
            if (BinaryOperator *BinOp = dyn_cast<BinaryOperator>(I)) {

                // Proviamo a interpretare i due operandi come costanti intere (se lo sono)
                ConstantInt *Op0 = dyn_cast<ConstantInt>(BinOp->getOperand(0));
                ConstantInt *Op1 = dyn_cast<ConstantInt>(BinOp->getOperand(1));
                Value *OtherOperand = nullptr; // Variabile per salvare l’operando non costante da riutilizzare 

                // Controlliamo se è un'addizione o una moltiplicazione con zero o uno: x + 0 -> x oppure 0 + x -> x
                if (BinOp->getOpcode() == Instruction::Add || BinOp->getOpcode() == Instruction::Sub) {
                    if(Op0 && !Op1 && Op0->getValue().isZero()) {
                        // 0 + x
                        OtherOperand = BinOp->getOperand(1);
                    } else if(Op1 && !Op0 && Op1->getValue().isZero()) {
                        // x + 0
                        OtherOperand = BinOp->getOperand(0);
                    } 
                // Ottimizzazione per moltiplicazione: x * 1 -> x oppure 1 * x -> x
                } else if (BinOp->getOpcode() == Instruction::Mul) {
                    if (Op0 && !Op1 && Op0->getValue().isOne()) {
                        // 1 * x
                        OtherOperand = BinOp->getOperand(1);
                    } else if (Op1 && !Op0 && Op1->getValue().isOne()) {
                        // x * 1
                        OtherOperand = BinOp->getOperand(0);  
                    }
                // Ottimizzazione per divisione: x / 1 -> x
                } else if (BinOp->getOpcode() == Instruction::SDiv) {
                    // Se Op0 è una variabile e Op1 non lo è allora, aggiungiamo Op0 al vettore
                    if (Op1 && !Op0 && Op1->getValue().isOne()) {
                        // x / 1
                        OtherOperand = BinOp->getOperand(0);
                    }
                }

                // Se abbiamo trovato un caso semplificabile, sostituiamo l'istruzione
                if (OtherOperand) {
                    // Stampiamo l'ottimizzazione rilevata su stderr (solo per debug)
                    errs() << "Semplifico: " << *dyn_cast<BinaryOperator>(I) << " -> uso " << *OtherOperand << "\n";
                    // Rimpiazziamo tutte le occorrenze dell'istruzione con il valore semplificato
                    I->replaceAllUsesWith(OtherOperand);
                    // Salviamo l'istruzione per eliminarla alla fine
                    InstructionsToRemove.push_back(I);
                    // Indichiamo che il blocco è stato modificato
                    Changed = true;
                }
            }
        }

        
        // Rimuoviamo tutte le istruzioni che sono state sostituite usiamo la dead code elimination 
        // per togliere le operazioni inutili dopo averle sostituite 
        for (Instruction *I : InstructionsToRemove) {
            I->eraseFromParent();
        }
        // Restituisce true se sono state fatte modifiche
        return Changed;
    }
    // Applichiamo il pass su ogni BasicBlock della funzione
    bool runOnFunction(Function &F) {
        bool Transformed = false; // Indica se almeno un blocco è stato modificato
        errs() << "Algebraic Identity:\n"; // Messaggio di debug che segnala l’inizio dell’analisi

        // Itera su tutti i BasicBlock della funzione, se almeno un blocco è stato trasformato, segnalo
        for (auto &BB : F) { if (runOnBasicBlock(BB)) { Transformed = true; } }
        return Transformed; // Ritorna true se è stata fatta almeno una modifica
    }

    // Entry point ufficiale del nuovo pass (secondo il PassManager moderno)
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
         // Se la funzione è stata modificata, dichiara che nessuna analisi è stata preservata
        if (runOnFunction(F)) { return PreservedAnalyses::none(); }
        return PreservedAnalyses::all(); // Altrimenti, segnala che tutte le analisi sono ancora valide
    }
    // Indica che questo pass è sempre richiesto (anche in presenza di -O0)
    static bool isRequired() { return true; }
};




// Strength Reduction
// Questo pass trasforma alcune moltiplicazioni o divisioni con costanti
// in operazioni di shift (che sono molto più leggere a livello di CPU).
// Esempio: x * 8 -> x << 3, oppure x / 2 -> x >> 1

struct StrengthReduction : PassInfoMixin<StrengthReduction> {

    // Funzione che crea una nuova istruzione di shift (sinistro o destro) per sostituire una moltiplicazione o divisione.  
    // ShiftLeft: true per moltiplicazioni (shl), false per divisioni (ashr).
    // ShiftAmount: quanti bit shiftare (equivale a log2 della costante, per 8 -> 3, perché 2^3 = 8).
    // positionVar: indice dell'operando variabile (0 o 1) dell’operando che NON è costante
    // AddOrSub: opzionale, 1 per sommare, -1 per sottrarre il valore originale allo shift (per costanti +1,-1 da potenze di due).
         
    BinaryOperator *createShift(Instruction *I, bool ShiftLeft, unsigned ShiftAmount, unsigned positionVar, int AddOrSub = 0) {
        BinaryOperator *Shift = nullptr;
        if (ShiftLeft) {
            // Shift a sinistra: è come moltiplicare per una potenza di 2
            errs() << "Entro in shiftleft\n";
            Shift = BinaryOperator::Create(Instruction::Shl, I->getOperand(positionVar), ConstantInt::get(I->getOperand(positionVar)->getType(), ShiftAmount), "shift");
            Shift->insertAfter(I); // Inserisce subito dopo l'istruzione originale
            errs() << "Shift di: " << ShiftAmount << "\n";
            // Eventuale somma o sottrazione per ottimizzare moltiplicazioni tipo x * (2^n + 1) oppure x * (2^n - 1)
            if (AddOrSub == 1) {
                errs() << "Entro in Add\n";
                BinaryOperator *Add = BinaryOperator::Create(Instruction::Add, Shift, I->getOperand(positionVar), "add");
                Add->insertAfter(Shift);
                return Add;
            } else if (AddOrSub == -1) {
                errs() << "Entro in Sub\n";
                BinaryOperator *Sub = BinaryOperator::Create(Instruction::Sub, Shift, I->getOperand(positionVar), "sub");
                Sub->insertAfter(Shift);
                return Sub;
            }
            return Shift;
        } else {
            // Shift a destra aritmetico (equivalente a divisione intera con segno)
            errs() << "Entro in shiftright\n";
            Shift = BinaryOperator::Create(Instruction::AShr, I->getOperand(positionVar), ConstantInt::get(I->getOperand(positionVar)->getType(), ShiftAmount), "shift");
            Shift->insertAfter(I);
            errs() << "Shift di: " << ShiftAmount << "\n";
            return Shift;
        }
    }
    // Applichiamo la strength reduction a un BasicBlock
    bool runOnBasicBlock(BasicBlock &B) {
        bool Changed = false;

        std::vector<Instruction*> InstructionsToRemove; // Istruzioni da rimuovere dopo la trasformazione
        
        // Scorriamo tutte le istruzioni del blocco
        for(Instruction &Inst : B){
            Instruction *I = &Inst;
            // Consideriamo solo le operazioni binarie
            if (BinaryOperator *BinOp = dyn_cast<BinaryOperator>(I)) {

                // Verifichiamo se gli operandi sono costanti intere
                ConstantInt *Op0 = dyn_cast<ConstantInt>(BinOp->getOperand(0));
                ConstantInt *Op1 = dyn_cast<ConstantInt>(BinOp->getOperand(1));
                Value *OtherOperand = nullptr;
                BinaryOperator *Reference = nullptr;

                // Moltiplicazione 
                if (BinOp->getOpcode() == Instruction::Mul) {
                    // Op0 è la costante a sinistra (numero), mentre Op1 è la variabile a destra (x)
                    if(Op0 && !Op1){
                        // Primo: x * 2^n -> x << n
                        if(Op0->getValue().isPowerOf2()){
                            I->replaceAllUsesWith(createShift(I, true, Op0->getValue().logBase2(),1));
                            InstructionsToRemove.push_back(I);
                            Changed = true;
                            errs() << "Semplifico: " << *dyn_cast<BinaryOperator>(I) <<"\n";
                        // Secondo: x * (2^n - 1) -> (x << n) - x
                        }else if((Op0->getValue()+1).isPowerOf2()){
                            I->replaceAllUsesWith(createShift(I, true, (Op0->getValue()+1).logBase2(),1,-1));
                            InstructionsToRemove.push_back(I);
                            Changed = true;
                            errs() << "Semplifico: " << *dyn_cast<BinaryOperator>(I) <<"\n";
                        // Terzo: x * (2^n + 1) -> (x << n) + x
                        }else if((Op0->getValue()-1).isPowerOf2()){ 
                            I->replaceAllUsesWith(createShift(I, true, (Op0->getValue()-1).logBase2(),1,1));
                            InstructionsToRemove.push_back(I);
                            Changed = true;
                            errs() << "Semplifico: " << *dyn_cast<BinaryOperator>(I) <<"\n";
                        }
                    }
                    //Simmetrico: Op0 è la varibile a sinistra, mentre Op1 è la costante a destra
                    else if (Op1 && !Op0)
                    {
                        // Primo simmetrico: x * 2^n -> x << n
                        if(Op1->getValue().isPowerOf2()){
                            I->replaceAllUsesWith(createShift(I, true, Op1->getValue().logBase2(),0));
                            InstructionsToRemove.push_back(I);
                            Changed = true;
                            errs() << "Semplifico: " << *dyn_cast<BinaryOperator>(I) <<"\n";
                        // Secondo simmetrico: x * (2^n - 1) -> (x << n) - x
                        }else if((Op1->getValue()+1).isPowerOf2()){
                            I->replaceAllUsesWith(createShift(I, true, (Op1->getValue()+1).logBase2(),0,-1));
                            InstructionsToRemove.push_back(I);
                            Changed = true;
                            errs() << "Semplifico: " << *dyn_cast<BinaryOperator>(I) <<"\n";
                        // Terzo simmetrico: x * (2^n + 1) -> (x << n) + x
                        }else if((Op1->getValue()-1).isPowerOf2()){
                            I->replaceAllUsesWith(createShift(I, true, (Op1->getValue()-1).logBase2(),0,1));
                            InstructionsToRemove.push_back(I);
                            Changed = true;
                            errs() << "Semplifico: " << *dyn_cast<BinaryOperator>(I) <<"\n";
                        }
                    }
                //Divisione (con costante potenza di 2): x / 2^n -> x >> n
                } else if (BinOp->getOpcode() == Instruction::SDiv) {
                    // Si fa il controllo solo sul secondo operando perchè ci interessa shiftare il denominatore
                    // non il numeratore (x/8)
                    if(Op1 && !Op0){
                        if(Op1->getValue().isPowerOf2()){
                            // Sostituisco la divisione con shift aritmetico a destra (>>)
                            I->replaceAllUsesWith(createShift(I, false, Op1->getValue().logBase2(),0));
                            InstructionsToRemove.push_back(I);
                            Changed = true;
                            errs() << "Semplifico: " << *dyn_cast<BinaryOperator>(I) <<"\n";
                        }
                    } 
                }
            }
        }

        // Rimuovi le vecchie istruzioni sostituite: usiamo la dead code elimination per togliere le operazioni 
        // inutili dopo averle sostituite
        for (Instruction *I : InstructionsToRemove) {
            I->eraseFromParent();
        }
        
        return Changed;
    }

    // Applica il pass a tutta la funzione
    bool runOnFunction(Function &F) {
        errs() << "Strength Reduction:\n"; // Output di errore
        bool Transformed = false;
        for (auto &BB : F) { if (runOnBasicBlock(BB)) { Transformed = true; } }
        return Transformed;
    }
    // Metodo richiesto dal PassManager: indica se l'analisi è ancora valida dopo questo pass
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
         if (runOnFunction(F)) { return PreservedAnalyses::none(); } // Le analisi non sono più valide
        return PreservedAnalyses::all(); // Nessuna modifica, analisi ancora valide
    }
    // Obbliga l'esecuzione del pass (anche in -O0)
    static bool isRequired() { return true; }
};





// Multi-Instruction Optimization
 
// Questo pass cerca di ottimizzare sequenze semplici e ravvicinate di operazioni
// che si annullano tra loro. 
// Esempio: a = b + 1; c = a - 1; -> c = b 
// L’obiettivo è riconoscere queste coppie e semplificarle, rimuovendo l’istruzione inutile.


struct MultiInstructionOpt : PassInfoMixin<MultiInstructionOpt> {
    
    // Applichiamo l'ottimizzazione a un singolo BasicBlock
     bool runOnBasicBlock(BasicBlock &B) { 

        bool Changed = false;

        std::vector<Instruction*> InstructionsToRemove; // Salva le istruzioni da rimuovere dopo la sostituzione

        // Esaminiamo tutte le istruzioni del blocco
        for (Instruction &Inst : B){
            Instruction *I = &Inst;
            // Consideriamo solo le istruzioni binarie (add, sub)
            if (BinaryOperator *BinOp = dyn_cast<BinaryOperator>(I)){
                // Prendiamo il segno dell'operazione, salvando l'operatore: Instruction::Add o Instruction::Sub
                unsigned int Operator = BinOp ->getOpcode();
                if (Operator == Instruction::Add || Operator == Instruction::Sub) {

                    //errs()<< "Prima istruzione: " << *dyn_cast<BinaryOperator>(I)<<"\n";

                    // Prendiamo e Castiamo i 2 operandi in ConstantInt (Prova a convertire i due operandi a costanti)
                    ConstantInt *COp0 = dyn_cast<ConstantInt>(BinOp->getOperand(0));
                    ConstantInt *COp1 = dyn_cast<ConstantInt>(BinOp ->getOperand(1));
                    // Creiamo variabili d'appoggio per salvare l'operando variabile (da cercare) e quello costante 
                    Value* Var = nullptr;
                    ConstantInt* Const = nullptr;
                    // Controlliamo se uno dei due è una variabile, se lo è, estrae la coppia <variabile, costante> dall'operazione binaria
                    if (Operator == Instruction::Add){
                        if (COp0 && !COp1){
                            Const = COp0;
                            Var = BinOp->getOperand(1);
                        }else if (!COp0 && COp1){
                            Const = COp1;
                            Var = BinOp->getOperand(0);
                        }
                    // Nel caso di sottrazione, accettiamo solo "variabile - costante"
                    }else if (COp1 && !COp0) {
                        Const = COp1;
                        Var = BinOp->getOperand(0);
                    }
                    // Se non sono costanti, non faccio nulla
                    
                    // Cerchiamo un’istruzione che usa il risultato della prima
                    for (auto SecondInst = I->user_begin(); SecondInst != I->user_end(); SecondInst++){
                        // Controlliamo che l'isruzione utente sia un operazione binaria
                        
                        //errs()<< "Seconda istruzione: " << *dyn_cast<BinaryOperator>(SecondInst)<<"\n";
                        if (BinaryOperator *SecondBinOp = dyn_cast<BinaryOperator>(*SecondInst)){
                            // Prendiamo e Castiamo a costanti per i controlli, estrae gli operandi della seconda istruzione (che usa la prima)
                            ConstantInt *SecondCOp0 = dyn_cast<ConstantInt>(SecondBinOp->getOperand(0));
                            ConstantInt *SecondCOp1 = dyn_cast<ConstantInt>(SecondBinOp->getOperand(1));
                            // Prendiamo l'operatore della seconda istruzione
                            unsigned int SecondOperator = SecondBinOp->getOpcode();

                            // Verifichiamo che le due operazioni siano opposte: prima è Add, seconda è Sub oppure viceversa
                            if ((Operator == Instruction::Sub && SecondOperator == Instruction::Add) || (Operator == Instruction::Add && SecondOperator == Instruction::Sub)){
                                //c = (a) + costante -> a = b - costante
                                if(SecondCOp0 && !SecondCOp1 && SecondOperator == Instruction::Add){
                                    if (Const->getValue() == SecondCOp0->getValue()){
                                        errs()<< "Prima istruzione: " << *dyn_cast<BinaryOperator>(I) << "\nSeconda istruzione: ";
                                        SecondInst->print(errs()); 
                                        errs()<< "\n\n";
                                        SecondBinOp->replaceAllUsesWith(Var);// Rimpiazza con la variabile iniziale    
                                        InstructionsToRemove.push_back(SecondBinOp);
                                        Changed = true;
                                    }
                                }
                                //c = (a) + costante -> costante come secondo operando
                                else if(!SecondCOp0 && SecondCOp1 && SecondOperator == Instruction::Add){
                                    errs()<< "Prima istruzione: " << *dyn_cast<BinaryOperator>(I) << "\nSeconda istruzione: ";
                                    SecondInst->print(errs()); 
                                    errs()<< "\n\n";
                                    if (Const->getValue() == SecondCOp1->getValue()){
                                        SecondBinOp->replaceAllUsesWith(Var);
                                        InstructionsToRemove.push_back(SecondBinOp);
                                        Changed = true;
                                    }
                                }
                                //c = (a) - costante -> la costante coincide con quella sommata prima
                                else if (!SecondCOp0 && SecondCOp1 && SecondOperator == Instruction::Sub){
                                    errs()<< "Prima istruzione: " << *dyn_cast<BinaryOperator>(I) << "\nSeconda istruzione: ";
                                    SecondInst->print(errs()); 
                                    errs()<< "\n\n";
                                    if (Const->getValue() == SecondCOp1->getValue()){
                                        SecondBinOp->replaceAllUsesWith(Var);
                                        InstructionsToRemove.push_back(SecondBinOp);
                                        Changed = true;   
                                    }
                                }
                            }
                        } 
                    }
                }
            }
        } 
        
        // Rimuove le istruzioni ormai inutili
        for (Instruction *I : InstructionsToRemove) {
            I->eraseFromParent();
        }

        return Changed;
    }

    // Applica l'ottimizzazione a tutti i blocchi della funzione
     bool runOnFunction(Function &F) {
        errs() << "Multi Instruction Optimization:\n";
        bool Transformed = false;
        for (auto &BB : F) { if (runOnBasicBlock(BB)) { Transformed = true; } }
        return Transformed;
    }

    // Metodo compatibile con il nuovo PassManager: segnala se il pass modifica qualcosa
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
        if (runOnFunction(F)) { return PreservedAnalyses::none(); } //Se ha fatto modifiche, invalidiamo le analisi -> none
        return PreservedAnalyses::all(); // Altrimenti, tutto è rimasto invariato -> all 
    }
    // Obbliga l'esecuzione del pass anche senza ottimizzazioni (-O0)
    static bool isRequired() { return true; }
};

}

// sono al telefono 
// Entry point richiesto da LLVM per i plugin caricabili dinamicamente
// Viene chiamato da `opt` per ottenere le informazioni del pass
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    // Restituisce una struct PassPluginLibraryInfo contenente:
    // - La versione API del plugin
    // - Il nome del plugin ("CombinedOpts")
    // - La versione di LLVM in uso
    // - Una lambda di registrazione per i pass definiti

    return {LLVM_PLUGIN_API_VERSION, "CombinedOpts", LLVM_VERSION_STRING,
            // Funzione di registrazione dei pass al PassBuilder
            [](PassBuilder &PB) {
                // Registra il primo passo, pass "algebraic-identity"
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                         // Se il nome corrisponde, aggiunge il pass al FunctionPassManager
                        if (Name == "algebraic-identity") {
                            FPM.addPass(AlgebraicIdentity()); // Aggiunge il pass personalizzato
                            return true;
                        }
                        return false; // Non è il nome giusto, passa al prossimo
                    });

                // Registra il secondo passo, pass "strength-reduction"
                 PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "strength-reduction") { 
                            FPM.addPass(StrengthReduction()); // Aggiunge il pass
                            return true;
                        }
                        return false;
                    });
                 // Registra il terzo passo, pass "multi-instruction-opt"
                 PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "multi-instruction-opt") {
                            FPM.addPass(MultiInstructionOpt()); // Aggiunge il pass
                            return true;
                        }
                        return false;
                    });
            }};
}
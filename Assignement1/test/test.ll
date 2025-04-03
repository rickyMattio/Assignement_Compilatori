; Funzione per Algebraic Identity
define dso_local i32 @algebraic_test(i32 %x, i32 %y) {
entry:
  %add1 = add nsw i32 %x, 0
  %mul1 = mul nsw i32 %y, 1
  %add2 = add nsw i32 %add1, %mul1 ; Dovrebbe diventare add nsw %x, %y
  ret i32 %add2
}

; Funzione per Strength Reduction
define dso_local i32 @strength_test(i32 %a, i32 %b) {
entry:
  %mul_res = mul nsw i32 %a, 15  ; Dovrebbe diventare shl/sub
  %div_res = sdiv i32 %b, 8      ; Dovrebbe diventare ashr
  %result = add nsw i32 %mul_res, %div_res
  ret i32 %result
}

; Funzione per Multi-Instruction Optimization
define dso_local i32 @multi_opt_test(i32 %in) {
entry:
  %add_one = add nsw i32 %in, 1     ; a = b + 1 (qui b è %in)
  %sub_one = sub nsw i32 %add_one, 1 ; c = a - 1
  ; %add_one deve avere solo %sub_one come user per l'ottimizzazione qui implementata
  ret i32 %sub_one                  ; Dovrebbe restituire direttamente %in
}

; Funzione che combina più casi
define dso_local i32 @combined_test(i32 %arg) {
entry:
  %t1 = add i32 %arg, 0          ; algebraic -> %arg
  %t2 = mul i32 %t1, 15          ; strength su %arg
  %t3 = add i32 %arg, 1          ; multi-opt parte 1
  %t4 = sub i32 %t3, 1           ; multi-opt parte 2 -> %arg (se t3 ha solo un uso)
  %t5 = add i32 %t2, %t4         ; Somma il risultato di strength e multi-opt
  ret i32 %t5
}

; Modulo LLVM IR per testare CombinedOpts

define dso_local i32 @test(i32 %x, i32 %y, i32 %z) {
entry:
  ; 1. Algebraic Identity targets
  %x_plus_0 = add i32 %x, 0          ; Dovrebbe diventare %x
  %zero_plus_y = add i32 0, %y       ; Dovrebbe diventare %y
  %z_times_1 = mul i32 %z, 1          ; Dovrebbe diventare %z
  %one_times_x = mul i32 1, %x_plus_0  ; Dovrebbe diventare %x (usa il risultato precedente)

  ; 2. Strength Reduction targets
  %mul_15_res = mul i32 %zero_plus_y, 15 ; Dovrebbe essere sostituito da shl+sub su %y
  %div_8_res = sdiv i32 %z_times_1, 8    ; Dovrebbe essere sostituito da ashr su %z

  ; 3. Multi-Instruction Optimization target
  %b_var = add i32 %one_times_x, 5     ; Un valore intermedio 'b'
  %a_var = add i32 %b_var, 1           ; a = b + 1
  %c_var = sub i32 %a_var, 1           ; c = a - 1 (dovrebbe essere sostituito da %b_var)

  ; Uso dei risultati per evitare che vengano eliminati come codice morto
  %temp1 = add i32 %mul_15_res, %div_8_res
  %final_result = add i32 %temp1, %c_var ; c_var dovrebbe essere rimpiazzato

  ret i32 %final_result
}

; Funzione aggiuntiva per testare argomenti
define dso_local i32 @another_test(i32 %arg1) {
  %val = add i32 %arg1, 0 ; Test identità su argomento
  ret i32 %val
}

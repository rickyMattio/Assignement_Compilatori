; ModuleID = '../test/test.ll'
source_filename = "../test/test.ll"

define dso_local i32 @test(i32 %x, i32 %y, i32 %z) {
entry:
  %shl4 = shl i32 %y, 4
  %subx = sub i32 %shl4, %y
  %ashr3 = ashr i32 %z, 3
  %b_var = add i32 %x, 5
  %a_var = add i32 %b_var, 1
  %temp1 = add i32 %subx, %ashr3
  %final_result = add i32 %temp1, %b_var
  ret i32 %final_result
}

define dso_local i32 @another_test(i32 %arg1) {
  ret i32 %arg1
}

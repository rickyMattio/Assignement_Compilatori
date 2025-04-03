; ModuleID = '../test/test.ll'
source_filename = "../test/test.ll"

define dso_local i32 @algebraic_test(i32 %x, i32 %y) {
entry:
  %add2 = add nsw i32 %x, %y
  ret i32 %add2
}

define dso_local i32 @strength_test(i32 %a, i32 %b) {
entry:
  %shl4 = shl i32 %a, 4
  %subx = sub i32 %shl4, %a
  %ashr3 = ashr i32 %b, 3
  %result = add nsw i32 %subx, %ashr3
  ret i32 %result
}

define dso_local i32 @multi_opt_test(i32 %in) {
entry:
  %add_one = add nsw i32 %in, 1
  %sub_one = sub nsw i32 %add_one, 1
  ret i32 %sub_one
}

define dso_local i32 @combined_test(i32 %arg) {
entry:
  %shl4 = shl i32 %arg, 4
  %subx = sub i32 %shl4, %arg
  %t3 = add i32 %arg, 1
  %t4 = sub i32 %t3, 1
  %t5 = add i32 %subx, %t4
  ret i32 %t5
}

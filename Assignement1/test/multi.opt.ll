; ModuleID = '../test/test.ll'
source_filename = "../test/test.ll"

define dso_local i32 @algebraic_test(i32 %x, i32 %y) {
entry:
  %add1 = add nsw i32 %x, 0
  %mul1 = mul nsw i32 %y, 1
  %add2 = add nsw i32 %add1, %mul1
  ret i32 %add2
}

define dso_local i32 @strength_test(i32 %a, i32 %b) {
entry:
  %mul_res = mul nsw i32 %a, 15
  %div_res = sdiv i32 %b, 8
  %result = add nsw i32 %mul_res, %div_res
  ret i32 %result
}

define dso_local i32 @multi_opt_test(i32 %in) {
entry:
  %add_one = add nsw i32 %in, 1
  ret i32 %in
}

define dso_local i32 @combined_test(i32 %arg) {
entry:
  %t1 = add i32 %arg, 0
  %t2 = mul i32 %t1, 15
  %t3 = add i32 %arg, 1
  %t5 = add i32 %t2, %arg
  ret i32 %t5
}

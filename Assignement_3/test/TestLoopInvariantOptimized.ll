; ModuleID = 'test/TestLoopInvariant2.bc'
source_filename = "TestLoopInvariant.cpp"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

; Function Attrs: mustprogress noinline nounwind uwtable
define dso_local noundef i32 @_Z12testFunctionii(i32 noundef %0, i32 noundef %1) #0 {
  %3 = add nsw i32 %0, %1
  %4 = mul nsw i32 %3, 10
  br label %5

5:                                                ; preds = %19, %2
  %.01 = phi i32 [ 0, %2 ], [ %.1, %19 ]
  %.0 = phi i32 [ 0, %2 ], [ %20, %19 ]
  %6 = icmp slt i32 %.0, 20
  br i1 %6, label %7, label %21

7:                                                ; preds = %5
  %8 = add nsw i32 8, %4
  %9 = mul nsw i32 %8, %.0
  %10 = srem i32 %.0, 2
  %11 = icmp eq i32 %10, 0
  br i1 %11, label %12, label %16

12:                                               ; preds = %7
  %13 = add nsw i32 %0, %1
  %14 = add nsw i32 %9, %13
  %15 = add nsw i32 %.01, %14
  br label %18

16:                                               ; preds = %7
  %17 = add nsw i32 %.01, %9
  br label %18

18:                                               ; preds = %16, %12
  %.1 = phi i32 [ %15, %12 ], [ %17, %16 ]
  br label %19

19:                                               ; preds = %18
  %20 = add nsw i32 %.0, 1
  br label %5, !llvm.loop !6

21:                                               ; preds = %5
  ret i32 %.01
}

; Function Attrs: mustprogress noinline norecurse nounwind uwtable
define dso_local noundef i32 @main() #1 {
  %1 = call noundef i32 @_Z12testFunctionii(i32 noundef 2, i32 noundef 3)
  ret i32 %1
}

attributes #0 = { mustprogress noinline nounwind uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { mustprogress noinline norecurse nounwind uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"Ubuntu clang version 19.1.7 (++20250114103320+cd708029e0b2-1~exp1~20250114103432.75)"}
!6 = distinct !{!6, !7}
!7 = !{!"llvm.loop.mustprogress"}

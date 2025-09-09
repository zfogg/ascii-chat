; ModuleID = 'lib/image2ascii/simd/avx2.c'
source_filename = "lib/image2ascii/simd/avx2.c"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-apple-macosx15.0.0"

%struct.outbuf_t = type { ptr, i64, i64 }
%struct.rgb_t = type { i8, i8, i8 }
%struct.utf8_char_t = type { [4 x i8], i8 }

@.str = private unnamed_addr constant [28 x i8] c"lib/image2ascii/simd/avx2.c\00", align 1
@__func__.render_ascii_image_monochrome_avx2 = private unnamed_addr constant [35 x i8] c"render_ascii_image_monochrome_avx2\00", align 1
@.str.1 = private unnamed_addr constant [34 x i8] c"Failed to get UTF-8 palette cache\00", align 1
@.str.2 = private unnamed_addr constant [52 x i8] c"Failed to allocate output buffer for AVX2 rendering\00", align 1
@__func__.render_ascii_avx2_unified_optimized = private unnamed_addr constant [36 x i8] c"render_ascii_avx2_unified_optimized\00", align 1
@.str.3 = private unnamed_addr constant [36 x i8] c"Memory allocation failed: %zu bytes\00", align 1
@.str.4 = private unnamed_addr constant [49 x i8] c"Failed to get UTF-8 palette cache for AVX2 color\00", align 1
@.str.5 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@__func__.avx2_caches_destroy = private unnamed_addr constant [20 x i8] c"avx2_caches_destroy\00", align 1
@.str.6 = private unnamed_addr constant [35 x i8] c"AVX2_CACHE: AVX2 caches cleaned up\00", align 1

; Function Attrs: nounwind ssp uwtable
define ptr @render_ascii_image_monochrome_avx2(ptr noundef readonly %0, ptr noundef %1) local_unnamed_addr #0 {
  %3 = alloca %struct.outbuf_t, align 8
  %4 = alloca [32 x i8], align 16
  %5 = icmp eq ptr %0, null
  br i1 %5, label %221, label %6

6:                                                ; preds = %2
  %7 = getelementptr inbounds i8, ptr %0, i64 8
  %8 = load ptr, ptr %7, align 8, !tbaa !6
  %9 = icmp ne ptr %8, null
  %10 = icmp ne ptr %1, null
  %11 = and i1 %10, %9
  br i1 %11, label %12, label %221

12:                                               ; preds = %6
  %13 = getelementptr inbounds i8, ptr %0, i64 4
  %14 = load i32, ptr %13, align 4, !tbaa !12
  %15 = load i32, ptr %0, align 8, !tbaa !13
  %16 = icmp slt i32 %14, 1
  %17 = icmp slt i32 %15, 1
  %18 = select i1 %16, i1 true, i1 %17
  br i1 %18, label %221, label %19

19:                                               ; preds = %12
  %20 = tail call ptr @get_utf8_palette_cache(ptr noundef nonnull %1) #9
  %21 = icmp eq ptr %20, null
  br i1 %21, label %22, label %23

22:                                               ; preds = %19
  tail call void (i32, ptr, i32, ptr, ptr, ...) @log_msg(i32 noundef 3, ptr noundef nonnull @.str, i32 noundef 123, ptr noundef nonnull @__func__.render_ascii_image_monochrome_avx2, ptr noundef nonnull @.str.1) #9
  br label %221

23:                                               ; preds = %19
  %24 = load ptr, ptr %7, align 8, !tbaa !6
  call void @llvm.lifetime.start.p0(i64 24, ptr nonnull %3) #9
  %25 = getelementptr inbounds i8, ptr %3, i64 8
  store i64 0, ptr %25, align 8
  %26 = zext nneg i32 %14 to i64
  %27 = zext nneg i32 %15 to i64
  %28 = shl nuw nsw i64 %27, 2
  %29 = or disjoint i64 %28, 1
  %30 = mul nuw i64 %29, %26
  %31 = getelementptr inbounds i8, ptr %3, i64 16
  store i64 %30, ptr %31, align 8, !tbaa !14
  %32 = tail call ptr @malloc(i64 noundef %30) #10
  store ptr %32, ptr %3, align 8, !tbaa !17
  %33 = icmp eq ptr %32, null
  br i1 %33, label %40, label %34

34:                                               ; preds = %23
  %35 = icmp ugt i32 %15, 31
  %36 = getelementptr inbounds i8, ptr %20, i64 1600
  %37 = getelementptr inbounds i8, ptr %20, i64 1280
  %38 = add nsw i32 %14, -1
  %39 = zext nneg i32 %38 to i64
  br label %43

40:                                               ; preds = %23
  tail call void (i32, ptr, i32, ptr, ptr, ...) @log_msg(i32 noundef 3, ptr noundef nonnull @.str, i32 noundef 134, ptr noundef nonnull @__func__.render_ascii_image_monochrome_avx2, ptr noundef nonnull @.str.2) #9
  br label %219

41:                                               ; preds = %216
  call void @ob_term(ptr noundef nonnull %3) #9
  %42 = load ptr, ptr %3, align 8, !tbaa !17
  br label %219

43:                                               ; preds = %34, %216
  %44 = phi i64 [ 0, %34 ], [ %217, %216 ]
  %45 = trunc nuw nsw i64 %44 to i32
  %46 = mul i32 %15, %45
  %47 = zext i32 %46 to i64
  %48 = getelementptr inbounds %struct.rgb_t, ptr %24, i64 %47
  br i1 %35, label %49, label %141

49:                                               ; preds = %43
  call void @llvm.lifetime.start.p0(i64 32, ptr nonnull %4) #9
  br label %50

50:                                               ; preds = %49, %136
  %51 = phi i32 [ 0, %49 ], [ %137, %136 ]
  %52 = zext i32 %51 to i64
  %53 = getelementptr inbounds %struct.rgb_t, ptr %48, i64 %52
  %54 = load <96 x i8>, ptr %53, align 1, !tbaa !18, !alias.scope !19, !noalias !22
  %55 = shufflevector <96 x i8> %54, <96 x i8> poison, <32 x i32> <i32 0, i32 3, i32 6, i32 9, i32 12, i32 15, i32 18, i32 21, i32 24, i32 27, i32 30, i32 33, i32 36, i32 39, i32 42, i32 45, i32 48, i32 51, i32 54, i32 57, i32 60, i32 63, i32 66, i32 69, i32 72, i32 75, i32 78, i32 81, i32 84, i32 87, i32 90, i32 93>
  %56 = shufflevector <96 x i8> %54, <96 x i8> poison, <32 x i32> <i32 1, i32 4, i32 7, i32 10, i32 13, i32 16, i32 19, i32 22, i32 25, i32 28, i32 31, i32 34, i32 37, i32 40, i32 43, i32 46, i32 49, i32 52, i32 55, i32 58, i32 61, i32 64, i32 67, i32 70, i32 73, i32 76, i32 79, i32 82, i32 85, i32 88, i32 91, i32 94>
  %57 = shufflevector <96 x i8> %54, <96 x i8> poison, <32 x i32> <i32 2, i32 5, i32 8, i32 11, i32 14, i32 17, i32 20, i32 23, i32 26, i32 29, i32 32, i32 35, i32 38, i32 41, i32 44, i32 47, i32 50, i32 53, i32 56, i32 59, i32 62, i32 65, i32 68, i32 71, i32 74, i32 77, i32 80, i32 83, i32 86, i32 89, i32 92, i32 95>
  %58 = shufflevector <32 x i8> %55, <32 x i8> <i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison>, <32 x i32> <i32 0, i32 32, i32 1, i32 33, i32 2, i32 34, i32 3, i32 35, i32 4, i32 36, i32 5, i32 37, i32 6, i32 38, i32 7, i32 39, i32 16, i32 48, i32 17, i32 49, i32 18, i32 50, i32 19, i32 51, i32 20, i32 52, i32 21, i32 53, i32 22, i32 54, i32 23, i32 55>
  %59 = shufflevector <32 x i8> %56, <32 x i8> <i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison>, <32 x i32> <i32 0, i32 32, i32 1, i32 33, i32 2, i32 34, i32 3, i32 35, i32 4, i32 36, i32 5, i32 37, i32 6, i32 38, i32 7, i32 39, i32 16, i32 48, i32 17, i32 49, i32 18, i32 50, i32 19, i32 51, i32 20, i32 52, i32 21, i32 53, i32 22, i32 54, i32 23, i32 55>
  %60 = shufflevector <32 x i8> %57, <32 x i8> <i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison>, <32 x i32> <i32 0, i32 32, i32 1, i32 33, i32 2, i32 34, i32 3, i32 35, i32 4, i32 36, i32 5, i32 37, i32 6, i32 38, i32 7, i32 39, i32 16, i32 48, i32 17, i32 49, i32 18, i32 50, i32 19, i32 51, i32 20, i32 52, i32 21, i32 53, i32 22, i32 54, i32 23, i32 55>
  %61 = bitcast <32 x i8> %58 to <16 x i16>
  %62 = mul nuw nsw <16 x i16> %61, <i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77>
  %63 = bitcast <32 x i8> %59 to <16 x i16>
  %64 = mul nuw <16 x i16> %63, <i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150>
  %65 = bitcast <32 x i8> %60 to <16 x i16>
  %66 = mul nuw nsw <16 x i16> %65, <i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29>
  %67 = add nuw <16 x i16> %62, <i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128>
  %68 = add <16 x i16> %67, %64
  %69 = add <16 x i16> %68, %66
  %70 = lshr <16 x i16> %69, <i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8>
  %71 = shufflevector <32 x i8> %55, <32 x i8> <i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0>, <32 x i32> <i32 8, i32 40, i32 9, i32 41, i32 10, i32 42, i32 11, i32 43, i32 12, i32 44, i32 13, i32 45, i32 14, i32 46, i32 15, i32 47, i32 24, i32 56, i32 25, i32 57, i32 26, i32 58, i32 27, i32 59, i32 28, i32 60, i32 29, i32 61, i32 30, i32 62, i32 31, i32 63>
  %72 = shufflevector <32 x i8> %56, <32 x i8> <i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0>, <32 x i32> <i32 8, i32 40, i32 9, i32 41, i32 10, i32 42, i32 11, i32 43, i32 12, i32 44, i32 13, i32 45, i32 14, i32 46, i32 15, i32 47, i32 24, i32 56, i32 25, i32 57, i32 26, i32 58, i32 27, i32 59, i32 28, i32 60, i32 29, i32 61, i32 30, i32 62, i32 31, i32 63>
  %73 = shufflevector <32 x i8> %57, <32 x i8> <i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0>, <32 x i32> <i32 8, i32 40, i32 9, i32 41, i32 10, i32 42, i32 11, i32 43, i32 12, i32 44, i32 13, i32 45, i32 14, i32 46, i32 15, i32 47, i32 24, i32 56, i32 25, i32 57, i32 26, i32 58, i32 27, i32 59, i32 28, i32 60, i32 29, i32 61, i32 30, i32 62, i32 31, i32 63>
  %74 = bitcast <32 x i8> %71 to <16 x i16>
  %75 = mul nuw nsw <16 x i16> %74, <i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77>
  %76 = bitcast <32 x i8> %72 to <16 x i16>
  %77 = mul nuw <16 x i16> %76, <i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150>
  %78 = bitcast <32 x i8> %73 to <16 x i16>
  %79 = mul nuw nsw <16 x i16> %78, <i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29>
  %80 = add nuw <16 x i16> %75, <i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128>
  %81 = add <16 x i16> %80, %77
  %82 = add <16 x i16> %81, %79
  %83 = lshr <16 x i16> %82, <i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8>
  %84 = call <32 x i8> @llvm.x86.avx2.packuswb(<16 x i16> %70, <16 x i16> %83)
  %85 = bitcast <32 x i8> %84 to <4 x i64>
  %86 = shufflevector <4 x i64> %85, <4 x i64> poison, <4 x i32> <i32 0, i32 2, i32 1, i32 3>
  %87 = bitcast <4 x i64> %86 to <32 x i8>
  %88 = lshr <32 x i8> %87, <i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2>
  store <32 x i8> %88, ptr %4, align 16, !tbaa !18
  %89 = icmp ult i32 %51, %15
  br i1 %89, label %90, label %136

90:                                               ; preds = %50, %132
  %91 = phi i32 [ %116, %132 ], [ 0, %50 ]
  %92 = zext nneg i32 %91 to i64
  %93 = getelementptr inbounds [32 x i8], ptr %4, i64 0, i64 %92
  %94 = load i8, ptr %93, align 1, !tbaa !18
  %95 = zext i8 %94 to i64
  %96 = getelementptr inbounds [64 x i8], ptr %36, i64 0, i64 %95
  %97 = load i8, ptr %96, align 1, !tbaa !18
  %98 = getelementptr inbounds [64 x %struct.utf8_char_t], ptr %37, i64 0, i64 %95
  br label %99

99:                                               ; preds = %107, %90
  %100 = phi i32 [ %114, %107 ], [ 1, %90 ]
  %101 = phi i64 [ %102, %107 ], [ %92, %90 ]
  %102 = add nuw nsw i64 %101, 1
  %103 = icmp ult i64 %101, 31
  %104 = or disjoint i64 %102, %52
  %105 = icmp ult i64 %104, %27
  %106 = select i1 %103, i1 %105, i1 false
  br i1 %106, label %107, label %115

107:                                              ; preds = %99
  %108 = getelementptr inbounds [32 x i8], ptr %4, i64 0, i64 %102
  %109 = load i8, ptr %108, align 1, !tbaa !18
  %110 = zext i8 %109 to i64
  %111 = getelementptr inbounds [64 x i8], ptr %36, i64 0, i64 %110
  %112 = load i8, ptr %111, align 1, !tbaa !18
  %113 = icmp eq i8 %112, %97
  %114 = add nuw nsw i32 %100, 1
  br i1 %113, label %99, label %115

115:                                              ; preds = %107, %99
  %116 = trunc nuw i64 %102 to i32
  %117 = sub nsw i32 %116, %91
  %118 = getelementptr inbounds i8, ptr %98, i64 4
  %119 = load i8, ptr %118, align 1, !tbaa !26
  %120 = zext i8 %119 to i64
  call void @ob_write(ptr noundef nonnull %3, ptr noundef nonnull %98, i64 noundef %120) #9
  %121 = call zeroext i1 @rep_is_profitable(i32 noundef %117) #9
  br i1 %121, label %124, label %122

122:                                              ; preds = %115
  %123 = icmp ugt i32 %117, 1
  br i1 %123, label %126, label %132

124:                                              ; preds = %115
  %125 = add i32 %117, -1
  call void @emit_rep(ptr noundef nonnull %3, i32 noundef %125) #9
  br label %132

126:                                              ; preds = %122, %126
  %127 = phi i32 [ %130, %126 ], [ 1, %122 ]
  %128 = load i8, ptr %118, align 1, !tbaa !26
  %129 = zext i8 %128 to i64
  call void @ob_write(ptr noundef nonnull %3, ptr noundef nonnull %98, i64 noundef %129) #9
  %130 = add nuw nsw i32 %127, 1
  %131 = icmp eq i32 %130, %100
  br i1 %131, label %132, label %126, !llvm.loop !28

132:                                              ; preds = %126, %122, %124
  %133 = or disjoint i32 %51, %116
  %134 = icmp ult i32 %133, %15
  %135 = select i1 %103, i1 %134, i1 false
  br i1 %135, label %90, label %136, !llvm.loop !30

136:                                              ; preds = %132, %50
  %137 = add i32 %51, 32
  %138 = or disjoint i32 %137, 31
  %139 = icmp ult i32 %138, %15
  br i1 %139, label %50, label %140, !llvm.loop !31

140:                                              ; preds = %136
  call void @llvm.lifetime.end.p0(i64 32, ptr nonnull %4) #9
  br label %141

141:                                              ; preds = %140, %43
  %142 = phi i32 [ %137, %140 ], [ 0, %43 ]
  %143 = icmp ult i32 %142, %15
  br i1 %143, label %144, label %213

144:                                              ; preds = %141, %211
  %145 = phi i32 [ %195, %211 ], [ %142, %141 ]
  %146 = zext nneg i32 %145 to i64
  %147 = getelementptr inbounds %struct.rgb_t, ptr %48, i64 %146
  %148 = load i8, ptr %147, align 1, !tbaa !32
  %149 = zext i8 %148 to i64
  %150 = mul nuw nsw i64 %149, 77
  %151 = getelementptr inbounds i8, ptr %147, i64 1
  %152 = load i8, ptr %151, align 1, !tbaa !34
  %153 = zext i8 %152 to i64
  %154 = mul nuw nsw i64 %153, 150
  %155 = getelementptr inbounds i8, ptr %147, i64 2
  %156 = load i8, ptr %155, align 1, !tbaa !35
  %157 = zext i8 %156 to i64
  %158 = mul nuw nsw i64 %157, 29
  %159 = add nuw nsw i64 %150, 128
  %160 = add nuw nsw i64 %159, %154
  %161 = add nuw nsw i64 %160, %158
  %162 = lshr i64 %161, 10
  %163 = getelementptr inbounds [64 x i8], ptr %36, i64 0, i64 %162
  %164 = load i8, ptr %163, align 1, !tbaa !18
  %165 = getelementptr inbounds [64 x %struct.utf8_char_t], ptr %37, i64 0, i64 %162
  %166 = add nuw nsw i32 %145, 1
  %167 = call i32 @llvm.umax.i32(i32 %15, i32 %166)
  br label %168

168:                                              ; preds = %172, %144
  %169 = phi i64 [ %170, %172 ], [ %146, %144 ]
  %170 = add nuw nsw i64 %169, 1
  %171 = icmp ult i64 %170, %27
  br i1 %171, label %172, label %194

172:                                              ; preds = %168
  %173 = getelementptr inbounds %struct.rgb_t, ptr %48, i64 %170
  %174 = load i8, ptr %173, align 1, !tbaa !32
  %175 = zext i8 %174 to i64
  %176 = mul nuw nsw i64 %175, 77
  %177 = getelementptr inbounds i8, ptr %173, i64 1
  %178 = load i8, ptr %177, align 1, !tbaa !34
  %179 = zext i8 %178 to i64
  %180 = mul nuw nsw i64 %179, 150
  %181 = getelementptr inbounds i8, ptr %173, i64 2
  %182 = load i8, ptr %181, align 1, !tbaa !35
  %183 = zext i8 %182 to i64
  %184 = mul nuw nsw i64 %183, 29
  %185 = add nuw nsw i64 %176, 128
  %186 = add nuw nsw i64 %185, %180
  %187 = add nuw nsw i64 %186, %184
  %188 = lshr i64 %187, 10
  %189 = getelementptr inbounds [64 x i8], ptr %36, i64 0, i64 %188
  %190 = load i8, ptr %189, align 1, !tbaa !18
  %191 = icmp eq i8 %190, %164
  br i1 %191, label %168, label %192

192:                                              ; preds = %172
  %193 = trunc nuw i64 %170 to i32
  br label %194

194:                                              ; preds = %168, %192
  %195 = phi i32 [ %193, %192 ], [ %167, %168 ]
  %196 = sub i32 %195, %145
  %197 = getelementptr inbounds i8, ptr %165, i64 4
  %198 = load i8, ptr %197, align 1, !tbaa !26
  %199 = zext i8 %198 to i64
  call void @ob_write(ptr noundef nonnull %3, ptr noundef nonnull %165, i64 noundef %199) #9
  %200 = call zeroext i1 @rep_is_profitable(i32 noundef %196) #9
  br i1 %200, label %203, label %201

201:                                              ; preds = %194
  %202 = icmp ugt i32 %196, 1
  br i1 %202, label %205, label %211

203:                                              ; preds = %194
  %204 = add i32 %196, -1
  call void @emit_rep(ptr noundef nonnull %3, i32 noundef %204) #9
  br label %211

205:                                              ; preds = %201, %205
  %206 = phi i32 [ %209, %205 ], [ 1, %201 ]
  %207 = load i8, ptr %197, align 1, !tbaa !26
  %208 = zext i8 %207 to i64
  call void @ob_write(ptr noundef nonnull %3, ptr noundef nonnull %165, i64 noundef %208) #9
  %209 = add nuw i32 %206, 1
  %210 = icmp eq i32 %209, %196
  br i1 %210, label %211, label %205, !llvm.loop !36

211:                                              ; preds = %205, %201, %203
  %212 = icmp ult i32 %195, %15
  br i1 %212, label %144, label %213, !llvm.loop !37

213:                                              ; preds = %211, %141
  %214 = icmp ult i64 %44, %39
  br i1 %214, label %215, label %216

215:                                              ; preds = %213
  call void @ob_putc(ptr noundef nonnull %3, i8 noundef signext 10) #9
  br label %216

216:                                              ; preds = %215, %213
  %217 = add nuw nsw i64 %44, 1
  %218 = icmp eq i64 %217, %26
  br i1 %218, label %41, label %43, !llvm.loop !38

219:                                              ; preds = %41, %40
  %220 = phi ptr [ %42, %41 ], [ null, %40 ]
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %3) #9
  br label %221

221:                                              ; preds = %12, %219, %22, %2, %6
  %222 = phi ptr [ null, %6 ], [ null, %2 ], [ null, %12 ], [ %220, %219 ], [ null, %22 ]
  ret ptr %222
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(i64 immarg, ptr nocapture) #1

declare ptr @get_utf8_palette_cache(ptr noundef) local_unnamed_addr #2

declare void @log_msg(i32 noundef, ptr noundef, i32 noundef, ptr noundef, ptr noundef, ...) local_unnamed_addr #2

; Function Attrs: mustprogress nofree nounwind willreturn allockind("alloc,uninitialized") allocsize(0) memory(inaccessiblemem: readwrite)
declare noalias noundef ptr @malloc(i64 noundef) local_unnamed_addr #3

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(i64 immarg, ptr nocapture) #1

declare void @ob_write(ptr noundef, ptr noundef, i64 noundef) local_unnamed_addr #2

declare zeroext i1 @rep_is_profitable(i32 noundef) local_unnamed_addr #2

declare void @emit_rep(ptr noundef, i32 noundef) local_unnamed_addr #2

declare void @ob_putc(ptr noundef, i8 noundef signext) local_unnamed_addr #2

declare void @ob_term(ptr noundef) local_unnamed_addr #2

; Function Attrs: nounwind ssp uwtable
define ptr @render_ascii_avx2_unified_optimized(ptr noundef readonly %0, i1 noundef zeroext %1, i1 noundef zeroext %2, ptr noundef %3) local_unnamed_addr #0 {
  %5 = alloca %struct.outbuf_t, align 8
  %6 = icmp eq ptr %0, null
  br i1 %6, label %500, label %7

7:                                                ; preds = %4
  %8 = getelementptr inbounds i8, ptr %0, i64 8
  %9 = load ptr, ptr %8, align 8, !tbaa !6
  %10 = icmp eq ptr %9, null
  br i1 %10, label %500, label %11

11:                                               ; preds = %7
  %12 = load i32, ptr %0, align 8, !tbaa !13
  %13 = getelementptr inbounds i8, ptr %0, i64 4
  %14 = load i32, ptr %13, align 4, !tbaa !12
  %15 = sext i32 %12 to i64
  %16 = sext i32 %14 to i64
  %17 = mul nsw i64 %16, %15
  %18 = icmp slt i32 %12, 1
  %19 = icmp slt i32 %14, 1
  %20 = select i1 %18, i1 true, i1 %19
  br i1 %20, label %21, label %26

21:                                               ; preds = %11
  %22 = tail call dereferenceable_or_null(1) ptr @malloc(i64 noundef 1) #10
  %23 = icmp eq ptr %22, null
  br i1 %23, label %24, label %25

24:                                               ; preds = %21
  tail call void (i32, ptr, i32, ptr, ptr, ...) @log_msg(i32 noundef 3, ptr noundef nonnull @.str, i32 noundef 246, ptr noundef nonnull @__func__.render_ascii_avx2_unified_optimized, ptr noundef nonnull @.str.3, i64 noundef 1) #9
  tail call void @exit(i32 noundef -1) #11
  unreachable

25:                                               ; preds = %21
  store i8 0, ptr %22, align 1, !tbaa !18
  br label %500

26:                                               ; preds = %11
  %27 = tail call ptr @get_utf8_palette_cache(ptr noundef %3) #9
  %28 = icmp eq ptr %27, null
  br i1 %28, label %29, label %30

29:                                               ; preds = %26
  tail call void (i32, ptr, i32, ptr, ptr, ...) @log_msg(i32 noundef 3, ptr noundef nonnull @.str, i32 noundef 254, ptr noundef nonnull @__func__.render_ascii_avx2_unified_optimized, ptr noundef nonnull @.str.4) #9
  br label %500

30:                                               ; preds = %26
  %31 = tail call ptr @malloc(i64 noundef %17) #10
  %32 = icmp eq ptr %31, null
  br i1 %32, label %33, label %34

33:                                               ; preds = %30
  tail call void (i32, ptr, i32, ptr, ptr, ...) @log_msg(i32 noundef 3, ptr noundef nonnull @.str, i32 noundef 268, ptr noundef nonnull @__func__.render_ascii_avx2_unified_optimized, ptr noundef nonnull @.str.3, i64 noundef %17) #9
  tail call void @exit(i32 noundef -1) #11
  unreachable

34:                                               ; preds = %30
  %35 = tail call ptr @malloc(i64 noundef %17) #10
  %36 = icmp eq ptr %35, null
  br i1 %36, label %37, label %38

37:                                               ; preds = %34
  tail call void (i32, ptr, i32, ptr, ptr, ...) @log_msg(i32 noundef 3, ptr noundef nonnull @.str, i32 noundef 271, ptr noundef nonnull @__func__.render_ascii_avx2_unified_optimized, ptr noundef nonnull @.str.3, i64 noundef %17) #9
  tail call void @exit(i32 noundef -1) #11
  unreachable

38:                                               ; preds = %34
  %39 = tail call ptr @malloc(i64 noundef %17) #10
  %40 = icmp eq ptr %39, null
  br i1 %40, label %41, label %42

41:                                               ; preds = %38
  tail call void (i32, ptr, i32, ptr, ptr, ...) @log_msg(i32 noundef 3, ptr noundef nonnull @.str, i32 noundef 272, ptr noundef nonnull @__func__.render_ascii_avx2_unified_optimized, ptr noundef nonnull @.str.3, i64 noundef %17) #9
  tail call void @exit(i32 noundef -1) #11
  unreachable

42:                                               ; preds = %38
  %43 = tail call ptr @malloc(i64 noundef %17) #10
  %44 = icmp eq ptr %43, null
  br i1 %44, label %45, label %46

45:                                               ; preds = %42
  tail call void (i32, ptr, i32, ptr, ptr, ...) @log_msg(i32 noundef 3, ptr noundef nonnull @.str, i32 noundef 273, ptr noundef nonnull @__func__.render_ascii_avx2_unified_optimized, ptr noundef nonnull @.str.3, i64 noundef %17) #9
  tail call void @exit(i32 noundef -1) #11
  unreachable

46:                                               ; preds = %42
  br i1 %2, label %47, label %51

47:                                               ; preds = %46
  %48 = tail call ptr @malloc(i64 noundef %17) #10
  %49 = icmp eq ptr %48, null
  br i1 %49, label %50, label %51

50:                                               ; preds = %47
  tail call void (i32, ptr, i32, ptr, ptr, ...) @log_msg(i32 noundef 3, ptr noundef nonnull @.str, i32 noundef 277, ptr noundef nonnull @__func__.render_ascii_avx2_unified_optimized, ptr noundef nonnull @.str.3, i64 noundef %17) #9
  tail call void @exit(i32 noundef -1) #11
  unreachable

51:                                               ; preds = %47, %46
  %52 = phi ptr [ %48, %47 ], [ null, %46 ]
  %53 = load ptr, ptr %8, align 8, !tbaa !6
  %54 = icmp ugt i64 %17, 31
  br i1 %54, label %191, label %55

55:                                               ; preds = %191, %51
  %56 = phi i64 [ 0, %51 ], [ %233, %191 ]
  %57 = icmp ult i64 %56, %17
  br i1 %57, label %58, label %261

58:                                               ; preds = %55
  br i1 %2, label %164, label %59

59:                                               ; preds = %58
  %60 = sub i64 %17, %56
  %61 = icmp ult i64 %60, 16
  br i1 %61, label %131, label %62

62:                                               ; preds = %59
  %63 = getelementptr i8, ptr %35, i64 %56
  %64 = getelementptr i8, ptr %35, i64 %17
  %65 = getelementptr i8, ptr %39, i64 %56
  %66 = getelementptr i8, ptr %39, i64 %17
  %67 = getelementptr i8, ptr %43, i64 %56
  %68 = getelementptr i8, ptr %43, i64 %17
  %69 = mul i64 %56, 3
  %70 = getelementptr i8, ptr %53, i64 %69
  %71 = mul nuw i64 %17, 3
  %72 = getelementptr i8, ptr %53, i64 %71
  %73 = icmp ult ptr %63, %66
  %74 = icmp ult ptr %65, %64
  %75 = and i1 %73, %74
  %76 = icmp ult ptr %63, %68
  %77 = icmp ult ptr %67, %64
  %78 = and i1 %76, %77
  %79 = or i1 %75, %78
  %80 = icmp ult ptr %63, %72
  %81 = icmp ult ptr %70, %64
  %82 = and i1 %80, %81
  %83 = or i1 %79, %82
  %84 = icmp ult ptr %65, %68
  %85 = icmp ult ptr %67, %66
  %86 = and i1 %84, %85
  %87 = or i1 %83, %86
  %88 = icmp ult ptr %65, %72
  %89 = icmp ult ptr %70, %66
  %90 = and i1 %88, %89
  %91 = or i1 %87, %90
  %92 = icmp ult ptr %67, %72
  %93 = icmp ult ptr %70, %68
  %94 = and i1 %92, %93
  %95 = or i1 %91, %94
  br i1 %95, label %131, label %96

96:                                               ; preds = %62
  %97 = icmp ult i64 %60, 32
  br i1 %97, label %133, label %98

98:                                               ; preds = %96
  %99 = and i64 %60, -32
  br label %100

100:                                              ; preds = %100, %98
  %101 = phi i64 [ 0, %98 ], [ %123, %100 ]
  %102 = add i64 %56, %101
  %103 = getelementptr inbounds %struct.rgb_t, ptr %53, i64 %102
  %104 = load <96 x i8>, ptr %103, align 1, !tbaa !18
  %105 = shufflevector <96 x i8> %104, <96 x i8> poison, <32 x i32> <i32 0, i32 3, i32 6, i32 9, i32 12, i32 15, i32 18, i32 21, i32 24, i32 27, i32 30, i32 33, i32 36, i32 39, i32 42, i32 45, i32 48, i32 51, i32 54, i32 57, i32 60, i32 63, i32 66, i32 69, i32 72, i32 75, i32 78, i32 81, i32 84, i32 87, i32 90, i32 93>
  %106 = shufflevector <96 x i8> %104, <96 x i8> poison, <32 x i32> <i32 1, i32 4, i32 7, i32 10, i32 13, i32 16, i32 19, i32 22, i32 25, i32 28, i32 31, i32 34, i32 37, i32 40, i32 43, i32 46, i32 49, i32 52, i32 55, i32 58, i32 61, i32 64, i32 67, i32 70, i32 73, i32 76, i32 79, i32 82, i32 85, i32 88, i32 91, i32 94>
  %107 = shufflevector <96 x i8> %104, <96 x i8> poison, <32 x i32> <i32 2, i32 5, i32 8, i32 11, i32 14, i32 17, i32 20, i32 23, i32 26, i32 29, i32 32, i32 35, i32 38, i32 41, i32 44, i32 47, i32 50, i32 53, i32 56, i32 59, i32 62, i32 65, i32 68, i32 71, i32 74, i32 77, i32 80, i32 83, i32 86, i32 89, i32 92, i32 95>
  %108 = getelementptr inbounds i8, ptr %35, i64 %102
  store <32 x i8> %105, ptr %108, align 1, !tbaa !18, !alias.scope !39, !noalias !42
  %109 = getelementptr inbounds i8, ptr %39, i64 %102
  store <32 x i8> %106, ptr %109, align 1, !tbaa !18, !alias.scope !46, !noalias !47
  %110 = getelementptr inbounds i8, ptr %43, i64 %102
  store <32 x i8> %107, ptr %110, align 1, !tbaa !18, !alias.scope !48, !noalias !49
  %111 = zext <32 x i8> %105 to <32 x i32>
  %112 = mul nuw nsw <32 x i32> %111, <i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77>
  %113 = zext <32 x i8> %106 to <32 x i32>
  %114 = mul nuw nsw <32 x i32> %113, <i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150>
  %115 = zext <32 x i8> %107 to <32 x i32>
  %116 = mul nuw nsw <32 x i32> %115, <i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29>
  %117 = add nuw nsw <32 x i32> %112, <i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128>
  %118 = add nuw nsw <32 x i32> %117, %114
  %119 = add nuw nsw <32 x i32> %118, %116
  %120 = lshr <32 x i32> %119, <i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10>
  %121 = trunc nuw <32 x i32> %120 to <32 x i8>
  %122 = getelementptr inbounds i8, ptr %31, i64 %102
  store <32 x i8> %121, ptr %122, align 1, !tbaa !18
  %123 = add nuw i64 %101, 32
  %124 = icmp eq i64 %123, %99
  br i1 %124, label %125, label %100, !llvm.loop !50

125:                                              ; preds = %100
  %126 = icmp eq i64 %60, %99
  br i1 %126, label %261, label %127

127:                                              ; preds = %125
  %128 = add i64 %56, %99
  %129 = and i64 %60, 16
  %130 = icmp eq i64 %129, 0
  br i1 %130, label %131, label %133

131:                                              ; preds = %162, %62, %59, %127
  %132 = phi i64 [ %56, %59 ], [ %56, %62 ], [ %128, %127 ], [ %136, %162 ]
  br label %236

133:                                              ; preds = %96, %127
  %134 = phi i64 [ %99, %127 ], [ 0, %96 ]
  %135 = and i64 %60, -16
  %136 = add i64 %56, %135
  br label %137

137:                                              ; preds = %137, %133
  %138 = phi i64 [ %134, %133 ], [ %160, %137 ]
  %139 = add i64 %56, %138
  %140 = getelementptr inbounds %struct.rgb_t, ptr %53, i64 %139
  %141 = load <48 x i8>, ptr %140, align 1, !tbaa !18
  %142 = shufflevector <48 x i8> %141, <48 x i8> poison, <16 x i32> <i32 0, i32 3, i32 6, i32 9, i32 12, i32 15, i32 18, i32 21, i32 24, i32 27, i32 30, i32 33, i32 36, i32 39, i32 42, i32 45>
  %143 = shufflevector <48 x i8> %141, <48 x i8> poison, <16 x i32> <i32 1, i32 4, i32 7, i32 10, i32 13, i32 16, i32 19, i32 22, i32 25, i32 28, i32 31, i32 34, i32 37, i32 40, i32 43, i32 46>
  %144 = shufflevector <48 x i8> %141, <48 x i8> poison, <16 x i32> <i32 2, i32 5, i32 8, i32 11, i32 14, i32 17, i32 20, i32 23, i32 26, i32 29, i32 32, i32 35, i32 38, i32 41, i32 44, i32 47>
  %145 = getelementptr inbounds i8, ptr %35, i64 %139
  store <16 x i8> %142, ptr %145, align 1, !tbaa !18, !alias.scope !53, !noalias !56
  %146 = getelementptr inbounds i8, ptr %39, i64 %139
  store <16 x i8> %143, ptr %146, align 1, !tbaa !18, !alias.scope !60, !noalias !61
  %147 = getelementptr inbounds i8, ptr %43, i64 %139
  store <16 x i8> %144, ptr %147, align 1, !tbaa !18, !alias.scope !62, !noalias !63
  %148 = zext <16 x i8> %142 to <16 x i32>
  %149 = mul nuw nsw <16 x i32> %148, <i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77, i32 77>
  %150 = zext <16 x i8> %143 to <16 x i32>
  %151 = mul nuw nsw <16 x i32> %150, <i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150, i32 150>
  %152 = zext <16 x i8> %144 to <16 x i32>
  %153 = mul nuw nsw <16 x i32> %152, <i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29, i32 29>
  %154 = add nuw nsw <16 x i32> %149, <i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128, i32 128>
  %155 = add nuw nsw <16 x i32> %154, %151
  %156 = add nuw nsw <16 x i32> %155, %153
  %157 = lshr <16 x i32> %156, <i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10, i32 10>
  %158 = trunc nuw <16 x i32> %157 to <16 x i8>
  %159 = getelementptr inbounds i8, ptr %31, i64 %139
  store <16 x i8> %158, ptr %159, align 1, !tbaa !18
  %160 = add nuw i64 %138, 16
  %161 = icmp eq i64 %160, %135
  br i1 %161, label %162, label %137, !llvm.loop !64

162:                                              ; preds = %137
  %163 = icmp eq i64 %60, %135
  br i1 %163, label %261, label %131

164:                                              ; preds = %58, %164
  %165 = phi i64 [ %189, %164 ], [ %56, %58 ]
  %166 = getelementptr inbounds %struct.rgb_t, ptr %53, i64 %165
  %167 = load i8, ptr %166, align 1, !tbaa !32
  %168 = getelementptr inbounds i8, ptr %166, i64 1
  %169 = load i8, ptr %168, align 1, !tbaa !34
  %170 = getelementptr inbounds i8, ptr %166, i64 2
  %171 = load i8, ptr %170, align 1, !tbaa !35
  %172 = getelementptr inbounds i8, ptr %35, i64 %165
  store i8 %167, ptr %172, align 1, !tbaa !18
  %173 = getelementptr inbounds i8, ptr %39, i64 %165
  store i8 %169, ptr %173, align 1, !tbaa !18
  %174 = getelementptr inbounds i8, ptr %43, i64 %165
  store i8 %171, ptr %174, align 1, !tbaa !18
  %175 = zext i8 %167 to i32
  %176 = mul nuw nsw i32 %175, 77
  %177 = zext i8 %169 to i32
  %178 = mul nuw nsw i32 %177, 150
  %179 = zext i8 %171 to i32
  %180 = mul nuw nsw i32 %179, 29
  %181 = add nuw nsw i32 %176, 128
  %182 = add nuw nsw i32 %181, %178
  %183 = add nuw nsw i32 %182, %180
  %184 = lshr i32 %183, 10
  %185 = trunc nuw nsw i32 %184 to i8
  %186 = getelementptr inbounds i8, ptr %31, i64 %165
  store i8 %185, ptr %186, align 1, !tbaa !18
  %187 = tail call zeroext i8 @rgb_to_256color(i8 noundef zeroext %167, i8 noundef zeroext %169, i8 noundef zeroext %171) #9
  %188 = getelementptr inbounds i8, ptr %52, i64 %165
  store i8 %187, ptr %188, align 1, !tbaa !18
  %189 = add i64 %165, 1
  %190 = icmp eq i64 %189, %17
  br i1 %190, label %261, label %164, !llvm.loop !65

191:                                              ; preds = %51, %191
  %192 = phi i64 [ %233, %191 ], [ 0, %51 ]
  %193 = getelementptr inbounds %struct.rgb_t, ptr %53, i64 %192
  %194 = getelementptr inbounds i8, ptr %31, i64 %192
  %195 = getelementptr inbounds i8, ptr %39, i64 %192
  %196 = load <96 x i8>, ptr %193, align 1, !tbaa !18, !alias.scope !66, !noalias !69
  %197 = shufflevector <96 x i8> %196, <96 x i8> poison, <32 x i32> <i32 0, i32 3, i32 6, i32 9, i32 12, i32 15, i32 18, i32 21, i32 24, i32 27, i32 30, i32 33, i32 36, i32 39, i32 42, i32 45, i32 48, i32 51, i32 54, i32 57, i32 60, i32 63, i32 66, i32 69, i32 72, i32 75, i32 78, i32 81, i32 84, i32 87, i32 90, i32 93>
  %198 = shufflevector <96 x i8> %196, <96 x i8> poison, <32 x i32> <i32 1, i32 4, i32 7, i32 10, i32 13, i32 16, i32 19, i32 22, i32 25, i32 28, i32 31, i32 34, i32 37, i32 40, i32 43, i32 46, i32 49, i32 52, i32 55, i32 58, i32 61, i32 64, i32 67, i32 70, i32 73, i32 76, i32 79, i32 82, i32 85, i32 88, i32 91, i32 94>
  %199 = shufflevector <96 x i8> %196, <96 x i8> poison, <32 x i32> <i32 2, i32 5, i32 8, i32 11, i32 14, i32 17, i32 20, i32 23, i32 26, i32 29, i32 32, i32 35, i32 38, i32 41, i32 44, i32 47, i32 50, i32 53, i32 56, i32 59, i32 62, i32 65, i32 68, i32 71, i32 74, i32 77, i32 80, i32 83, i32 86, i32 89, i32 92, i32 95>
  %200 = getelementptr inbounds i8, ptr %35, i64 %192
  %201 = getelementptr inbounds i8, ptr %43, i64 %192
  store <32 x i8> %197, ptr %200, align 1
  store <32 x i8> %198, ptr %195, align 1
  store <32 x i8> %199, ptr %201, align 1
  %202 = shufflevector <32 x i8> %197, <32 x i8> <i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison>, <32 x i32> <i32 0, i32 32, i32 1, i32 33, i32 2, i32 34, i32 3, i32 35, i32 4, i32 36, i32 5, i32 37, i32 6, i32 38, i32 7, i32 39, i32 16, i32 48, i32 17, i32 49, i32 18, i32 50, i32 19, i32 51, i32 20, i32 52, i32 21, i32 53, i32 22, i32 54, i32 23, i32 55>
  %203 = shufflevector <32 x i8> %198, <32 x i8> <i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison>, <32 x i32> <i32 0, i32 32, i32 1, i32 33, i32 2, i32 34, i32 3, i32 35, i32 4, i32 36, i32 5, i32 37, i32 6, i32 38, i32 7, i32 39, i32 16, i32 48, i32 17, i32 49, i32 18, i32 50, i32 19, i32 51, i32 20, i32 52, i32 21, i32 53, i32 22, i32 54, i32 23, i32 55>
  %204 = shufflevector <32 x i8> %199, <32 x i8> <i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison>, <32 x i32> <i32 0, i32 32, i32 1, i32 33, i32 2, i32 34, i32 3, i32 35, i32 4, i32 36, i32 5, i32 37, i32 6, i32 38, i32 7, i32 39, i32 16, i32 48, i32 17, i32 49, i32 18, i32 50, i32 19, i32 51, i32 20, i32 52, i32 21, i32 53, i32 22, i32 54, i32 23, i32 55>
  %205 = bitcast <32 x i8> %202 to <16 x i16>
  %206 = mul nuw nsw <16 x i16> %205, <i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77>
  %207 = bitcast <32 x i8> %203 to <16 x i16>
  %208 = mul nuw <16 x i16> %207, <i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150>
  %209 = bitcast <32 x i8> %204 to <16 x i16>
  %210 = mul nuw nsw <16 x i16> %209, <i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29>
  %211 = add nuw <16 x i16> %206, <i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128>
  %212 = add <16 x i16> %211, %208
  %213 = add <16 x i16> %212, %210
  %214 = lshr <16 x i16> %213, <i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8>
  %215 = shufflevector <32 x i8> %197, <32 x i8> <i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0>, <32 x i32> <i32 8, i32 40, i32 9, i32 41, i32 10, i32 42, i32 11, i32 43, i32 12, i32 44, i32 13, i32 45, i32 14, i32 46, i32 15, i32 47, i32 24, i32 56, i32 25, i32 57, i32 26, i32 58, i32 27, i32 59, i32 28, i32 60, i32 29, i32 61, i32 30, i32 62, i32 31, i32 63>
  %216 = shufflevector <32 x i8> %198, <32 x i8> <i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0>, <32 x i32> <i32 8, i32 40, i32 9, i32 41, i32 10, i32 42, i32 11, i32 43, i32 12, i32 44, i32 13, i32 45, i32 14, i32 46, i32 15, i32 47, i32 24, i32 56, i32 25, i32 57, i32 26, i32 58, i32 27, i32 59, i32 28, i32 60, i32 29, i32 61, i32 30, i32 62, i32 31, i32 63>
  %217 = shufflevector <32 x i8> %199, <32 x i8> <i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 poison, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0, i8 0>, <32 x i32> <i32 8, i32 40, i32 9, i32 41, i32 10, i32 42, i32 11, i32 43, i32 12, i32 44, i32 13, i32 45, i32 14, i32 46, i32 15, i32 47, i32 24, i32 56, i32 25, i32 57, i32 26, i32 58, i32 27, i32 59, i32 28, i32 60, i32 29, i32 61, i32 30, i32 62, i32 31, i32 63>
  %218 = bitcast <32 x i8> %215 to <16 x i16>
  %219 = mul nuw nsw <16 x i16> %218, <i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77, i16 77>
  %220 = bitcast <32 x i8> %216 to <16 x i16>
  %221 = mul nuw <16 x i16> %220, <i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150, i16 150>
  %222 = bitcast <32 x i8> %217 to <16 x i16>
  %223 = mul nuw nsw <16 x i16> %222, <i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29, i16 29>
  %224 = add nuw <16 x i16> %219, <i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128, i16 128>
  %225 = add <16 x i16> %224, %221
  %226 = add <16 x i16> %225, %223
  %227 = lshr <16 x i16> %226, <i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8, i16 8>
  %228 = tail call <32 x i8> @llvm.x86.avx2.packuswb(<16 x i16> %214, <16 x i16> %227)
  %229 = bitcast <32 x i8> %228 to <4 x i64>
  %230 = shufflevector <4 x i64> %229, <4 x i64> poison, <4 x i32> <i32 0, i32 2, i32 1, i32 3>
  %231 = bitcast <4 x i64> %230 to <32 x i8>
  %232 = lshr <32 x i8> %231, <i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2, i8 2>
  store <32 x i8> %232, ptr %194, align 1, !tbaa !18
  %233 = add i64 %192, 32
  %234 = or disjoint i64 %233, 31
  %235 = icmp ult i64 %234, %17
  br i1 %235, label %191, label %55, !llvm.loop !73

236:                                              ; preds = %131, %236
  %237 = phi i64 [ %259, %236 ], [ %132, %131 ]
  %238 = getelementptr inbounds %struct.rgb_t, ptr %53, i64 %237
  %239 = load i8, ptr %238, align 1, !tbaa !32
  %240 = getelementptr inbounds i8, ptr %238, i64 1
  %241 = load i8, ptr %240, align 1, !tbaa !34
  %242 = getelementptr inbounds i8, ptr %238, i64 2
  %243 = load i8, ptr %242, align 1, !tbaa !35
  %244 = getelementptr inbounds i8, ptr %35, i64 %237
  store i8 %239, ptr %244, align 1, !tbaa !18
  %245 = getelementptr inbounds i8, ptr %39, i64 %237
  store i8 %241, ptr %245, align 1, !tbaa !18
  %246 = getelementptr inbounds i8, ptr %43, i64 %237
  store i8 %243, ptr %246, align 1, !tbaa !18
  %247 = zext i8 %239 to i32
  %248 = mul nuw nsw i32 %247, 77
  %249 = zext i8 %241 to i32
  %250 = mul nuw nsw i32 %249, 150
  %251 = zext i8 %243 to i32
  %252 = mul nuw nsw i32 %251, 29
  %253 = add nuw nsw i32 %248, 128
  %254 = add nuw nsw i32 %253, %250
  %255 = add nuw nsw i32 %254, %252
  %256 = lshr i32 %255, 10
  %257 = trunc nuw nsw i32 %256 to i8
  %258 = getelementptr inbounds i8, ptr %31, i64 %237
  store i8 %257, ptr %258, align 1, !tbaa !18
  %259 = add nuw i64 %237, 1
  %260 = icmp eq i64 %259, %17
  br i1 %260, label %261, label %236, !llvm.loop !74

261:                                              ; preds = %236, %164, %125, %162, %55
  call void @llvm.lifetime.start.p0(i64 24, ptr nonnull %5) #9
  %262 = getelementptr inbounds i8, ptr %5, i64 8
  store i64 0, ptr %262, align 8
  %263 = select i1 %2, i64 10, i64 25
  %264 = mul i64 %17, %263
  %265 = shl nuw nsw i64 %16, 4
  %266 = add nuw nsw i64 %265, 1024
  %267 = add i64 %266, %264
  %268 = getelementptr inbounds i8, ptr %5, i64 16
  store i64 %267, ptr %268, align 8, !tbaa !14
  %269 = tail call i64 @llvm.umax.i64(i64 %267, i64 1)
  %270 = tail call ptr @malloc(i64 noundef %269) #10
  store ptr %270, ptr %5, align 8, !tbaa !17
  %271 = icmp eq ptr %270, null
  br i1 %271, label %492, label %272

272:                                              ; preds = %261
  %273 = getelementptr inbounds i8, ptr %27, i64 1600
  %274 = getelementptr inbounds i8, ptr %27, i64 1280
  %275 = add nsw i32 %14, -1
  br i1 %2, label %276, label %403

276:                                              ; preds = %272
  br i1 %1, label %277, label %340

277:                                              ; preds = %276, %282
  %278 = phi i32 [ %321, %282 ], [ -1, %276 ]
  %279 = phi i32 [ %283, %282 ], [ 0, %276 ]
  %280 = mul nuw nsw i32 %279, %12
  br label %285

281:                                              ; preds = %338
  call void @ob_write(ptr noundef nonnull %5, ptr noundef nonnull @.str.5, i64 noundef 1) #9
  br label %282

282:                                              ; preds = %338, %281
  %283 = add nuw nsw i32 %279, 1
  %284 = icmp eq i32 %283, %14
  br i1 %284, label %493, label %277, !llvm.loop !75

285:                                              ; preds = %336, %277
  %286 = phi i32 [ %278, %277 ], [ %321, %336 ]
  %287 = phi i32 [ 0, %277 ], [ %300, %336 ]
  %288 = add nsw i32 %287, %280
  %289 = sext i32 %288 to i64
  %290 = getelementptr inbounds i8, ptr %31, i64 %289
  %291 = load i8, ptr %290, align 1, !tbaa !18
  %292 = zext i8 %291 to i64
  %293 = getelementptr inbounds [64 x i8], ptr %273, i64 0, i64 %292
  %294 = load i8, ptr %293, align 1, !tbaa !18
  %295 = getelementptr inbounds [64 x %struct.utf8_char_t], ptr %274, i64 0, i64 %292
  %296 = getelementptr inbounds i8, ptr %52, i64 %289
  %297 = load i8, ptr %296, align 1, !tbaa !18
  br label %298

298:                                              ; preds = %311, %285
  %299 = phi i32 [ 1, %285 ], [ %315, %311 ]
  %300 = add i32 %299, %287
  %301 = icmp ult i32 %300, %12
  br i1 %301, label %302, label %316

302:                                              ; preds = %298
  %303 = add i32 %299, %288
  %304 = zext i32 %303 to i64
  %305 = getelementptr inbounds i8, ptr %31, i64 %304
  %306 = load i8, ptr %305, align 1, !tbaa !18
  %307 = zext i8 %306 to i64
  %308 = getelementptr inbounds [64 x i8], ptr %273, i64 0, i64 %307
  %309 = load i8, ptr %308, align 1, !tbaa !18
  %310 = icmp eq i8 %309, %294
  br i1 %310, label %311, label %316

311:                                              ; preds = %302
  %312 = getelementptr inbounds i8, ptr %52, i64 %304
  %313 = load i8, ptr %312, align 1, !tbaa !18
  %314 = icmp eq i8 %313, %297
  %315 = add i32 %299, 1
  br i1 %314, label %298, label %316

316:                                              ; preds = %311, %302, %298
  %317 = zext i8 %297 to i32
  %318 = icmp eq i32 %286, %317
  br i1 %318, label %320, label %319

319:                                              ; preds = %316
  call void @emit_set_256_color_bg(ptr noundef nonnull %5, i8 noundef zeroext %297) #9
  br label %320

320:                                              ; preds = %319, %316
  %321 = phi i32 [ %286, %316 ], [ %317, %319 ]
  %322 = getelementptr inbounds i8, ptr %295, i64 4
  %323 = load i8, ptr %322, align 1, !tbaa !26
  %324 = zext i8 %323 to i64
  call void @ob_write(ptr noundef nonnull %5, ptr noundef nonnull %295, i64 noundef %324) #9
  %325 = call zeroext i1 @rep_is_profitable(i32 noundef %299) #9
  br i1 %325, label %334, label %326

326:                                              ; preds = %320
  %327 = icmp ugt i32 %299, 1
  br i1 %327, label %328, label %336

328:                                              ; preds = %326, %328
  %329 = phi i32 [ %332, %328 ], [ 1, %326 ]
  %330 = load i8, ptr %322, align 1, !tbaa !26
  %331 = zext i8 %330 to i64
  call void @ob_write(ptr noundef nonnull %5, ptr noundef nonnull %295, i64 noundef %331) #9
  %332 = add nuw i32 %329, 1
  %333 = icmp eq i32 %332, %299
  br i1 %333, label %336, label %328, !llvm.loop !76

334:                                              ; preds = %320
  %335 = add i32 %299, -1
  call void @emit_rep(ptr noundef nonnull %5, i32 noundef %335) #9
  br label %336

336:                                              ; preds = %328, %334, %326
  %337 = icmp slt i32 %300, %12
  br i1 %337, label %285, label %338, !llvm.loop !77

338:                                              ; preds = %336
  %339 = icmp slt i32 %279, %275
  br i1 %339, label %281, label %282

340:                                              ; preds = %276, %345
  %341 = phi i32 [ %384, %345 ], [ -1, %276 ]
  %342 = phi i32 [ %346, %345 ], [ 0, %276 ]
  %343 = mul nuw nsw i32 %342, %12
  br label %348

344:                                              ; preds = %401
  call void @ob_write(ptr noundef nonnull %5, ptr noundef nonnull @.str.5, i64 noundef 1) #9
  br label %345

345:                                              ; preds = %401, %344
  %346 = add nuw nsw i32 %342, 1
  %347 = icmp eq i32 %346, %14
  br i1 %347, label %493, label %340, !llvm.loop !75

348:                                              ; preds = %397, %340
  %349 = phi i32 [ %341, %340 ], [ %384, %397 ]
  %350 = phi i32 [ 0, %340 ], [ %363, %397 ]
  %351 = add nsw i32 %350, %343
  %352 = sext i32 %351 to i64
  %353 = getelementptr inbounds i8, ptr %31, i64 %352
  %354 = load i8, ptr %353, align 1, !tbaa !18
  %355 = zext i8 %354 to i64
  %356 = getelementptr inbounds [64 x i8], ptr %273, i64 0, i64 %355
  %357 = load i8, ptr %356, align 1, !tbaa !18
  %358 = getelementptr inbounds [64 x %struct.utf8_char_t], ptr %274, i64 0, i64 %355
  %359 = getelementptr inbounds i8, ptr %52, i64 %352
  %360 = load i8, ptr %359, align 1, !tbaa !18
  br label %361

361:                                              ; preds = %374, %348
  %362 = phi i32 [ 1, %348 ], [ %378, %374 ]
  %363 = add i32 %362, %350
  %364 = icmp ult i32 %363, %12
  br i1 %364, label %365, label %379

365:                                              ; preds = %361
  %366 = add i32 %362, %351
  %367 = zext i32 %366 to i64
  %368 = getelementptr inbounds i8, ptr %31, i64 %367
  %369 = load i8, ptr %368, align 1, !tbaa !18
  %370 = zext i8 %369 to i64
  %371 = getelementptr inbounds [64 x i8], ptr %273, i64 0, i64 %370
  %372 = load i8, ptr %371, align 1, !tbaa !18
  %373 = icmp eq i8 %372, %357
  br i1 %373, label %374, label %379

374:                                              ; preds = %365
  %375 = getelementptr inbounds i8, ptr %52, i64 %367
  %376 = load i8, ptr %375, align 1, !tbaa !18
  %377 = icmp eq i8 %376, %360
  %378 = add i32 %362, 1
  br i1 %377, label %361, label %379

379:                                              ; preds = %374, %365, %361
  %380 = zext i8 %360 to i32
  %381 = icmp eq i32 %349, %380
  br i1 %381, label %383, label %382

382:                                              ; preds = %379
  call void @emit_set_256_color_fg(ptr noundef nonnull %5, i8 noundef zeroext %360) #9
  br label %383

383:                                              ; preds = %382, %379
  %384 = phi i32 [ %349, %379 ], [ %380, %382 ]
  %385 = getelementptr inbounds i8, ptr %358, i64 4
  %386 = load i8, ptr %385, align 1, !tbaa !26
  %387 = zext i8 %386 to i64
  call void @ob_write(ptr noundef nonnull %5, ptr noundef nonnull %358, i64 noundef %387) #9
  %388 = call zeroext i1 @rep_is_profitable(i32 noundef %362) #9
  br i1 %388, label %395, label %399

389:                                              ; preds = %399, %389
  %390 = phi i32 [ %393, %389 ], [ 1, %399 ]
  %391 = load i8, ptr %385, align 1, !tbaa !26
  %392 = zext i8 %391 to i64
  call void @ob_write(ptr noundef nonnull %5, ptr noundef nonnull %358, i64 noundef %392) #9
  %393 = add nuw i32 %390, 1
  %394 = icmp eq i32 %393, %362
  br i1 %394, label %397, label %389, !llvm.loop !76

395:                                              ; preds = %383
  %396 = add i32 %362, -1
  call void @emit_rep(ptr noundef nonnull %5, i32 noundef %396) #9
  br label %397

397:                                              ; preds = %389, %399, %395
  %398 = icmp slt i32 %363, %12
  br i1 %398, label %348, label %401, !llvm.loop !77

399:                                              ; preds = %383
  %400 = icmp ugt i32 %362, 1
  br i1 %400, label %389, label %397

401:                                              ; preds = %397
  %402 = icmp slt i32 %342, %275
  br i1 %402, label %344, label %345

403:                                              ; preds = %272, %410
  %404 = phi i32 [ %473, %410 ], [ -1, %272 ]
  %405 = phi i32 [ %472, %410 ], [ -1, %272 ]
  %406 = phi i32 [ %471, %410 ], [ -1, %272 ]
  %407 = phi i32 [ %411, %410 ], [ 0, %272 ]
  %408 = mul nuw nsw i32 %407, %12
  br label %413

409:                                              ; preds = %490
  call void @ob_write(ptr noundef nonnull %5, ptr noundef nonnull @.str.5, i64 noundef 1) #9
  br label %410

410:                                              ; preds = %409, %490
  %411 = add nuw nsw i32 %407, 1
  %412 = icmp eq i32 %411, %14
  br i1 %412, label %493, label %403, !llvm.loop !75

413:                                              ; preds = %403, %486
  %414 = phi i32 [ %404, %403 ], [ %473, %486 ]
  %415 = phi i32 [ %405, %403 ], [ %472, %486 ]
  %416 = phi i32 [ %406, %403 ], [ %471, %486 ]
  %417 = phi i32 [ 0, %403 ], [ %434, %486 ]
  %418 = add nsw i32 %417, %408
  %419 = sext i32 %418 to i64
  %420 = getelementptr inbounds i8, ptr %31, i64 %419
  %421 = load i8, ptr %420, align 1, !tbaa !18
  %422 = zext i8 %421 to i64
  %423 = getelementptr inbounds [64 x i8], ptr %273, i64 0, i64 %422
  %424 = load i8, ptr %423, align 1, !tbaa !18
  %425 = getelementptr inbounds [64 x %struct.utf8_char_t], ptr %274, i64 0, i64 %422
  %426 = getelementptr inbounds i8, ptr %35, i64 %419
  %427 = load i8, ptr %426, align 1, !tbaa !18
  %428 = getelementptr inbounds i8, ptr %39, i64 %419
  %429 = load i8, ptr %428, align 1, !tbaa !18
  %430 = getelementptr inbounds i8, ptr %43, i64 %419
  %431 = load i8, ptr %430, align 1, !tbaa !18
  br label %432

432:                                              ; preds = %453, %413
  %433 = phi i32 [ 1, %413 ], [ %457, %453 ]
  %434 = add i32 %433, %417
  %435 = icmp ult i32 %434, %12
  br i1 %435, label %436, label %458

436:                                              ; preds = %432
  %437 = add i32 %433, %418
  %438 = zext i32 %437 to i64
  %439 = getelementptr inbounds i8, ptr %31, i64 %438
  %440 = load i8, ptr %439, align 1, !tbaa !18
  %441 = zext i8 %440 to i64
  %442 = getelementptr inbounds [64 x i8], ptr %273, i64 0, i64 %441
  %443 = load i8, ptr %442, align 1, !tbaa !18
  %444 = icmp eq i8 %443, %424
  br i1 %444, label %445, label %458

445:                                              ; preds = %436
  %446 = getelementptr inbounds i8, ptr %35, i64 %438
  %447 = load i8, ptr %446, align 1, !tbaa !18
  %448 = icmp eq i8 %447, %427
  br i1 %448, label %449, label %458

449:                                              ; preds = %445
  %450 = getelementptr inbounds i8, ptr %39, i64 %438
  %451 = load i8, ptr %450, align 1, !tbaa !18
  %452 = icmp eq i8 %451, %429
  br i1 %452, label %453, label %458

453:                                              ; preds = %449
  %454 = getelementptr inbounds i8, ptr %43, i64 %438
  %455 = load i8, ptr %454, align 1, !tbaa !18
  %456 = icmp eq i8 %455, %431
  %457 = add i32 %433, 1
  br i1 %456, label %432, label %458

458:                                              ; preds = %453, %449, %445, %436, %432
  %459 = zext i8 %427 to i32
  %460 = icmp eq i32 %414, %459
  %461 = zext i8 %429 to i32
  %462 = icmp eq i32 %415, %461
  %463 = select i1 %460, i1 %462, i1 false
  %464 = zext i8 %431 to i32
  %465 = icmp eq i32 %416, %464
  %466 = select i1 %463, i1 %465, i1 false
  br i1 %466, label %470, label %467

467:                                              ; preds = %458
  br i1 %1, label %469, label %468

468:                                              ; preds = %467
  call void @emit_set_truecolor_fg(ptr noundef nonnull %5, i8 noundef zeroext %427, i8 noundef zeroext %429, i8 noundef zeroext %431) #9
  br label %470

469:                                              ; preds = %467
  call void @emit_set_truecolor_bg(ptr noundef nonnull %5, i8 noundef zeroext %427, i8 noundef zeroext %429, i8 noundef zeroext %431) #9
  br label %470

470:                                              ; preds = %469, %468, %458
  %471 = phi i32 [ %416, %458 ], [ %464, %468 ], [ %464, %469 ]
  %472 = phi i32 [ %415, %458 ], [ %461, %468 ], [ %461, %469 ]
  %473 = phi i32 [ %414, %458 ], [ %459, %468 ], [ %459, %469 ]
  %474 = getelementptr inbounds i8, ptr %425, i64 4
  %475 = load i8, ptr %474, align 1, !tbaa !26
  %476 = zext i8 %475 to i64
  call void @ob_write(ptr noundef nonnull %5, ptr noundef nonnull %425, i64 noundef %476) #9
  %477 = call zeroext i1 @rep_is_profitable(i32 noundef %433) #9
  br i1 %477, label %484, label %488

478:                                              ; preds = %488, %478
  %479 = phi i32 [ %482, %478 ], [ 1, %488 ]
  %480 = load i8, ptr %474, align 1, !tbaa !26
  %481 = zext i8 %480 to i64
  call void @ob_write(ptr noundef nonnull %5, ptr noundef nonnull %425, i64 noundef %481) #9
  %482 = add nuw i32 %479, 1
  %483 = icmp eq i32 %482, %433
  br i1 %483, label %486, label %478, !llvm.loop !78

484:                                              ; preds = %470
  %485 = add i32 %433, -1
  call void @emit_rep(ptr noundef nonnull %5, i32 noundef %485) #9
  br label %486

486:                                              ; preds = %478, %488, %484
  %487 = icmp slt i32 %434, %12
  br i1 %487, label %413, label %490, !llvm.loop !77

488:                                              ; preds = %470
  %489 = icmp ugt i32 %433, 1
  br i1 %489, label %478, label %486

490:                                              ; preds = %486
  %491 = icmp slt i32 %407, %275
  br i1 %491, label %409, label %410

492:                                              ; preds = %261
  tail call void @free(ptr noundef nonnull %31)
  tail call void @free(ptr noundef nonnull %35)
  tail call void @free(ptr noundef nonnull %39)
  tail call void @free(ptr noundef nonnull %43)
  br label %498

493:                                              ; preds = %410, %345, %282
  call void @ob_term(ptr noundef nonnull %5) #9
  call void @free(ptr noundef %31)
  call void @free(ptr noundef %35)
  call void @free(ptr noundef %39)
  call void @free(ptr noundef %43)
  %494 = icmp eq ptr %52, null
  br i1 %494, label %496, label %495

495:                                              ; preds = %493
  call void @free(ptr noundef nonnull %52)
  br label %496

496:                                              ; preds = %495, %493
  %497 = load ptr, ptr %5, align 8, !tbaa !17
  br label %498

498:                                              ; preds = %496, %492
  %499 = phi ptr [ %497, %496 ], [ null, %492 ]
  call void @llvm.lifetime.end.p0(i64 24, ptr nonnull %5) #9
  br label %500

500:                                              ; preds = %25, %498, %29, %4, %7
  %501 = phi ptr [ null, %7 ], [ null, %4 ], [ %22, %25 ], [ %499, %498 ], [ null, %29 ]
  ret ptr %501
}

; Function Attrs: noreturn
declare void @exit(i32 noundef) local_unnamed_addr #4

declare zeroext i8 @rgb_to_256color(i8 noundef zeroext, i8 noundef zeroext, i8 noundef zeroext) local_unnamed_addr #2

; Function Attrs: mustprogress nounwind willreturn allockind("free") memory(argmem: readwrite, inaccessiblemem: readwrite)
declare void @free(ptr allocptr nocapture noundef) local_unnamed_addr #5

declare void @emit_set_256_color_bg(ptr noundef, i8 noundef zeroext) local_unnamed_addr #2

declare void @emit_set_256_color_fg(ptr noundef, i8 noundef zeroext) local_unnamed_addr #2

declare void @emit_set_truecolor_bg(ptr noundef, i8 noundef zeroext, i8 noundef zeroext, i8 noundef zeroext) local_unnamed_addr #2

declare void @emit_set_truecolor_fg(ptr noundef, i8 noundef zeroext, i8 noundef zeroext, i8 noundef zeroext) local_unnamed_addr #2

; Function Attrs: nounwind ssp uwtable
define void @avx2_caches_destroy() local_unnamed_addr #6 {
  tail call void (i32, ptr, i32, ptr, ptr, ...) @log_msg(i32 noundef 0, ptr noundef nonnull @.str, i32 noundef 452, ptr noundef nonnull @__func__.avx2_caches_destroy, ptr noundef nonnull @.str.6) #9
  ret void
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(none)
declare <32 x i8> @llvm.x86.avx2.packuswb(<16 x i16>, <16 x i16>) #7

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i64 @llvm.umax.i64(i64, i64) #8

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i32 @llvm.umax.i32(i32, i32) #8

attributes #0 = { nounwind ssp uwtable "darwin-stkchk-strong-link" "frame-pointer"="all" "min-legal-vector-width"="256" "no-trapping-math"="true" "probe-stack"="___chkstk_darwin" "stack-protector-buffer-size"="8" "target-cpu"="penryn" "target-features"="+avx,+avx2,+cmov,+crc32,+cx16,+cx8,+fxsr,+mmx,+popcnt,+sahf,+sse,+sse2,+sse3,+sse4.1,+sse4.2,+ssse3,+x87,+xsave" "tune-cpu"="generic" }
attributes #1 = { mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { "darwin-stkchk-strong-link" "frame-pointer"="all" "no-trapping-math"="true" "probe-stack"="___chkstk_darwin" "stack-protector-buffer-size"="8" "target-cpu"="penryn" "target-features"="+avx,+avx2,+cmov,+crc32,+cx16,+cx8,+fxsr,+mmx,+popcnt,+sahf,+sse,+sse2,+sse3,+sse4.1,+sse4.2,+ssse3,+x87,+xsave" "tune-cpu"="generic" }
attributes #3 = { mustprogress nofree nounwind willreturn allockind("alloc,uninitialized") allocsize(0) memory(inaccessiblemem: readwrite) "alloc-family"="malloc" "darwin-stkchk-strong-link" "frame-pointer"="all" "no-trapping-math"="true" "probe-stack"="___chkstk_darwin" "stack-protector-buffer-size"="8" "target-cpu"="penryn" "target-features"="+avx,+avx2,+cmov,+crc32,+cx16,+cx8,+fxsr,+mmx,+popcnt,+sahf,+sse,+sse2,+sse3,+sse4.1,+sse4.2,+ssse3,+x87,+xsave" "tune-cpu"="generic" }
attributes #4 = { noreturn "darwin-stkchk-strong-link" "frame-pointer"="all" "no-trapping-math"="true" "probe-stack"="___chkstk_darwin" "stack-protector-buffer-size"="8" "target-cpu"="penryn" "target-features"="+avx,+avx2,+cmov,+crc32,+cx16,+cx8,+fxsr,+mmx,+popcnt,+sahf,+sse,+sse2,+sse3,+sse4.1,+sse4.2,+ssse3,+x87,+xsave" "tune-cpu"="generic" }
attributes #5 = { mustprogress nounwind willreturn allockind("free") memory(argmem: readwrite, inaccessiblemem: readwrite) "alloc-family"="malloc" "darwin-stkchk-strong-link" "frame-pointer"="all" "no-trapping-math"="true" "probe-stack"="___chkstk_darwin" "stack-protector-buffer-size"="8" "target-cpu"="penryn" "target-features"="+avx,+avx2,+cmov,+crc32,+cx16,+cx8,+fxsr,+mmx,+popcnt,+sahf,+sse,+sse2,+sse3,+sse4.1,+sse4.2,+ssse3,+x87,+xsave" "tune-cpu"="generic" }
attributes #6 = { nounwind ssp uwtable "darwin-stkchk-strong-link" "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "probe-stack"="___chkstk_darwin" "stack-protector-buffer-size"="8" "target-cpu"="penryn" "target-features"="+avx,+avx2,+cmov,+crc32,+cx16,+cx8,+fxsr,+mmx,+popcnt,+sahf,+sse,+sse2,+sse3,+sse4.1,+sse4.2,+ssse3,+x87,+xsave" "tune-cpu"="generic" }
attributes #7 = { mustprogress nocallback nofree nosync nounwind willreturn memory(none) }
attributes #8 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #9 = { nounwind }
attributes #10 = { allocsize(0) }
attributes #11 = { noreturn nounwind }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 15, i32 5]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 8, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"Apple clang version 17.0.0 (clang-1700.0.13.5)"}
!6 = !{!7, !11, i64 8}
!7 = !{!"image_t", !8, i64 0, !8, i64 4, !11, i64 8}
!8 = !{!"int", !9, i64 0}
!9 = !{!"omnipotent char", !10, i64 0}
!10 = !{!"Simple C/C++ TBAA"}
!11 = !{!"any pointer", !9, i64 0}
!12 = !{!7, !8, i64 4}
!13 = !{!7, !8, i64 0}
!14 = !{!15, !16, i64 16}
!15 = !{!"", !11, i64 0, !16, i64 8, !16, i64 16}
!16 = !{!"long", !9, i64 0}
!17 = !{!15, !11, i64 0}
!18 = !{!9, !9, i64 0}
!19 = !{!20}
!20 = distinct !{!20, !21, !"avx2_load_rgb32_optimized: argument 0"}
!21 = distinct !{!21, !"avx2_load_rgb32_optimized"}
!22 = !{!23, !24, !25}
!23 = distinct !{!23, !21, !"avx2_load_rgb32_optimized: argument 1"}
!24 = distinct !{!24, !21, !"avx2_load_rgb32_optimized: argument 2"}
!25 = distinct !{!25, !21, !"avx2_load_rgb32_optimized: argument 3"}
!26 = !{!27, !9, i64 4}
!27 = !{!"", !9, i64 0, !9, i64 4}
!28 = distinct !{!28, !29}
!29 = !{!"llvm.loop.mustprogress"}
!30 = distinct !{!30, !29}
!31 = distinct !{!31, !29}
!32 = !{!33, !9, i64 0}
!33 = !{!"rgb_t", !9, i64 0, !9, i64 1, !9, i64 2}
!34 = !{!33, !9, i64 1}
!35 = !{!33, !9, i64 2}
!36 = distinct !{!36, !29}
!37 = distinct !{!37, !29}
!38 = distinct !{!38, !29}
!39 = !{!40}
!40 = distinct !{!40, !41}
!41 = distinct !{!41, !"LVerDomain"}
!42 = !{!43, !44, !45}
!43 = distinct !{!43, !41}
!44 = distinct !{!44, !41}
!45 = distinct !{!45, !41}
!46 = !{!43}
!47 = !{!44, !45}
!48 = !{!44}
!49 = !{!45}
!50 = distinct !{!50, !29, !51, !52}
!51 = !{!"llvm.loop.isvectorized", i32 1}
!52 = !{!"llvm.loop.unroll.runtime.disable"}
!53 = !{!54}
!54 = distinct !{!54, !55}
!55 = distinct !{!55, !"LVerDomain"}
!56 = !{!57, !58, !59}
!57 = distinct !{!57, !55}
!58 = distinct !{!58, !55}
!59 = distinct !{!59, !55}
!60 = !{!57}
!61 = !{!58, !59}
!62 = !{!58}
!63 = !{!59}
!64 = distinct !{!64, !29, !51, !52}
!65 = distinct !{!65, !29}
!66 = !{!67}
!67 = distinct !{!67, !68, !"avx2_load_rgb32_optimized: argument 0"}
!68 = distinct !{!68, !"avx2_load_rgb32_optimized"}
!69 = !{!70, !71, !72}
!70 = distinct !{!70, !68, !"avx2_load_rgb32_optimized: argument 1"}
!71 = distinct !{!71, !68, !"avx2_load_rgb32_optimized: argument 2"}
!72 = distinct !{!72, !68, !"avx2_load_rgb32_optimized: argument 3"}
!73 = distinct !{!73, !29}
!74 = distinct !{!74, !29, !51}
!75 = distinct !{!75, !29}
!76 = distinct !{!76, !29}
!77 = distinct !{!77, !29}
!78 = distinct !{!78, !29}

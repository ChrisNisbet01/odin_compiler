#include "ir_intrinsic.h"

#include "llvm_ir_generator.h"

#include <string.h>

void
ir_gen_runtime_intrinsic_body(IrGenContext * ctx, char const * func_name,
                              TypeDescriptor const * proc_type)
{
    (void)proc_type;

    LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef i8_type = LLVMInt8TypeInContext(ctx->context);

    // Build syscall inline asm type: i64 (i64, i64, i64, i64)
    LLVMTypeRef asm_param_types[4] = {i64_type, i64_type, i64_type, i64_type};
    LLVMTypeRef asm_ftype = LLVMFunctionType(i64_type, asm_param_types, 4, false);
    LLVMValueRef asm_val = LLVMGetInlineAsm(
        asm_ftype,
        "syscall", 7,
        "={rax},{rax},{rdi},{rsi},{rdx},~{rcx},~{r11},~{memory}", 57,
        true, false, LLVMInlineAsmDialectATT, false
    );
    LLVMValueRef syscall_no = LLVMConstInt(i64_type, 1, false); // SYS_write
    LLVMValueRef current_func = func_current_function(ctx);

    if (strcmp(func_name, "print_string") == 0)
    {
        // Parameters: (context_ptr, fd: int, str: string)
        // string = {ptr data, i64 len}
        LLVMValueRef fd_param = LLVMGetParam(current_func, 1);
        LLVMValueRef str_param = LLVMGetParam(current_func, 2);

        // Cast fd to i64
        LLVMTypeRef fd_type_llvm = LLVMTypeOf(fd_param);
        if (LLVMGetTypeKind(fd_type_llvm) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(fd_type_llvm) != 64)
            fd_param = LLVMBuildIntCast2(ctx->builder, fd_param, i64_type, false, "ps.fd.ext");

        // Extract data pointer and length from string struct
        LLVMValueRef data_ptr = LLVMBuildExtractValue(ctx->builder, str_param, 0, "ps.data");
        LLVMValueRef len_val = LLVMBuildExtractValue(ctx->builder, str_param, 1, "ps.len");
        LLVMValueRef buf_i64 = LLVMBuildPtrToInt(ctx->builder, data_ptr, i64_type, "ps.buf");

        LLVMValueRef asm_args[4] = {syscall_no, fd_param, buf_i64, len_val};
        LLVMBuildCall2(ctx->builder, asm_ftype, asm_val, asm_args, 4, "");
        LLVMBuildRetVoid(ctx->builder);
    }
    else if (strcmp(func_name, "print_byte") == 0)
    {
        // Parameters: (context_ptr, fd: int, b: u8)
        LLVMValueRef fd_param = LLVMGetParam(current_func, 1);
        LLVMValueRef byte_val = LLVMGetParam(current_func, 2);

        // Cast fd to i64
        LLVMTypeRef fd_type_llvm = LLVMTypeOf(fd_param);
        if (LLVMGetTypeKind(fd_type_llvm) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(fd_type_llvm) != 64)
            fd_param = LLVMBuildIntCast2(ctx->builder, fd_param, i64_type, false, "pb.fd.ext");

        // Truncate byte to i8 if wider
        if (LLVMGetIntTypeWidth(LLVMTypeOf(byte_val)) != 8)
            byte_val = LLVMBuildTrunc(ctx->builder, byte_val, i8_type, "pb.trunc");

        LLVMValueRef byte_alloca = LLVMBuildAlloca(ctx->builder, i8_type, "pb.byte");
        LLVMBuildStore(ctx->builder, byte_val, byte_alloca);
        LLVMValueRef buf_i64 = LLVMBuildPtrToInt(ctx->builder, byte_alloca, i64_type, "pb.buf");

        LLVMValueRef asm_args[4] = {syscall_no, fd_param, buf_i64, LLVMConstInt(i64_type, 1, false)};
        LLVMBuildCall2(ctx->builder, asm_ftype, asm_val, asm_args, 4, "");
        LLVMBuildRetVoid(ctx->builder);
    }
    else if (strcmp(func_name, "int_to_string") == 0)
    {
        // Parameters: (context_ptr, i: int)
        LLVMValueRef i64_val = LLVMGetParam(current_func, 1);

        LLVMTypeRef val_type = LLVMTypeOf(i64_val);
        if (LLVMGetTypeKind(val_type) != LLVMIntegerTypeKind)
        {
            LLVMBuildUnreachable(ctx->builder);
            return;
        }

        // Extend to i64 if smaller
        if (LLVMGetIntTypeWidth(val_type) < 64)
            i64_val = LLVMBuildSExt(ctx->builder, i64_val, i64_type, "its.ext");
        else if (LLVMGetIntTypeWidth(val_type) > 64)
            i64_val = LLVMBuildTrunc(ctx->builder, i64_val, i64_type, "its.trunc");

        LLVMValueRef zero = LLVMConstInt(i64_type, 0, false);

        // Sign check and absolute value
        LLVMValueRef is_neg = LLVMBuildICmp(ctx->builder, LLVMIntSLT, i64_val, zero, "its.isneg");
        LLVMValueRef neg_val = LLVMBuildSub(ctx->builder, zero, i64_val, "its.neg");
        LLVMValueRef abs_val = LLVMBuildSelect(ctx->builder, is_neg, neg_val, i64_val, "its.abs");

        LLVMValueRef abs_saved = LLVMBuildAlloca(ctx->builder, i64_type, "its.abs.saved");
        LLVMBuildStore(ctx->builder, abs_val, abs_saved);
        LLVMValueRef n_digits_a = LLVMBuildAlloca(ctx->builder, i64_type, "its.ndigits");
        LLVMBuildStore(ctx->builder, zero, n_digits_a);
        LLVMValueRef temp_a = LLVMBuildAlloca(ctx->builder, i64_type, "its.temp");
        LLVMBuildStore(ctx->builder, abs_val, temp_a);

        LLVMBasicBlockRef ck_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "its.ck");
        LLVMBasicBlockRef cb_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "its.cb");
        LLVMBasicBlockRef cd_bb = LLVMAppendBasicBlockInContext(ctx->context, current_func, "its.cd");
        LLVMBuildBr(ctx->builder, ck_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, ck_bb);
        LLVMValueRef nd = LLVMBuildLoad2(ctx->builder, i64_type, n_digits_a, "its.nd");
        LLVMValueRef tp = LLVMBuildLoad2(ctx->builder, i64_type, temp_a, "its.tp");
        LLVMValueRef first = LLVMBuildICmp(ctx->builder, LLVMIntEQ, nd, zero, "its.first");
        LLVMValueRef still = LLVMBuildICmp(ctx->builder, LLVMIntUGT, tp, zero, "its.still");
        LLVMValueRef run = LLVMBuildOr(ctx->builder, first, still, "its.run");
        LLVMBuildCondBr(ctx->builder, run, cb_bb, cd_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, cb_bb);
        LLVMBuildStore(ctx->builder, LLVMBuildAdd(ctx->builder, nd, LLVMConstInt(i64_type, 1, false), "its.nd+"), n_digits_a);
        LLVMBuildStore(ctx->builder, LLVMBuildUDiv(ctx->builder, tp, LLVMConstInt(i64_type, 10, false), "its.tp/"), temp_a);
        LLVMBuildBr(ctx->builder, ck_bb);

        LLVMPositionBuilderAtEnd(ctx->builder, cd_bb);
        LLVMValueRef n_digits = LLVMBuildLoad2(ctx->builder, i64_type, n_digits_a, "its.n");
        LLVMValueRef sx = LLVMBuildZExt(ctx->builder, is_neg, i64_type, "its.sx");
        LLVMValueRef total_len = LLVMBuildAdd(ctx->builder, n_digits, sx, "its.len");

        LLVMValueRef buf_a = LLVMBuildAlloca(ctx->builder, LLVMArrayType(i8_type, 21), "its.buf");
        LLVMValueRef buf_p = LLVMBuildBitCast(ctx->builder, buf_a, LLVMPointerType(i8_type, 0), "its.bp");

        LLVMValueRef rem_a = LLVMBuildAlloca(ctx->builder, i64_type, "its.rem");
        LLVMBuildStore(ctx->builder, LLVMBuildLoad2(ctx->builder, i64_type, abs_saved, "its.abs"), rem_a);
        LLVMValueRef pos_a = LLVMBuildAlloca(ctx->builder, i64_type, "its.pos");
        LLVMBuildStore(ctx->builder, LLVMConstInt(i64_type, 20, false), pos_a);

        LLVMBasicBlockRef fck = LLVMAppendBasicBlockInContext(ctx->context, current_func, "its.fck");
        LLVMBasicBlockRef fbd = LLVMAppendBasicBlockInContext(ctx->context, current_func, "its.fbd");
        LLVMBasicBlockRef fdn = LLVMAppendBasicBlockInContext(ctx->context, current_func, "its.fdn");
        LLVMMoveBasicBlockAfter(fck, cd_bb);
        LLVMMoveBasicBlockAfter(fbd, fck);
        LLVMMoveBasicBlockAfter(fdn, fbd);
        LLVMBuildBr(ctx->builder, fck);

        LLVMPositionBuilderAtEnd(ctx->builder, fck);
        LLVMValueRef pv = LLVMBuildLoad2(ctx->builder, i64_type, pos_a, "its.pv");
        LLVMValueRef rv = LLVMBuildLoad2(ctx->builder, i64_type, rem_a, "its.rv");
        LLVMValueRef atend = LLVMBuildICmp(ctx->builder, LLVMIntEQ, pv, LLVMConstInt(i64_type, 20, false), "its.atend");
        LLVMValueRef more = LLVMBuildICmp(ctx->builder, LLVMIntUGT, rv, zero, "its.more");
        LLVMBuildCondBr(ctx->builder, LLVMBuildOr(ctx->builder, atend, more, "its.runfill"), fbd, fdn);

        LLVMPositionBuilderAtEnd(ctx->builder, fbd);
        LLVMValueRef np_ = LLVMBuildSub(ctx->builder, pv, LLVMConstInt(i64_type, 1, false), "its.p-");
        LLVMBuildStore(ctx->builder, np_, pos_a);
        LLVMValueRef ch = LLVMBuildTrunc(ctx->builder,
            LLVMBuildAdd(ctx->builder, LLVMBuildURem(ctx->builder, rv, LLVMConstInt(i64_type, 10, false), "its.digit"),
                LLVMConstInt(i64_type, '0', false), "its.ch"), i8_type, "its.ch8");
        LLVMValueRef cp = LLVMBuildInBoundsGEP2(ctx->builder, i8_type, buf_p, &np_, 1, "its.cp");
        LLVMBuildStore(ctx->builder, ch, cp);
        LLVMBuildStore(ctx->builder, LLVMBuildUDiv(ctx->builder, rv, LLVMConstInt(i64_type, 10, false), "its.r/"), rem_a);
        LLVMBuildBr(ctx->builder, fck);

        LLVMPositionBuilderAtEnd(ctx->builder, fdn);
        LLVMValueRef fp = LLVMBuildLoad2(ctx->builder, i64_type, pos_a, "its.fp");
        LLVMValueRef neg_pos = LLVMBuildSub(ctx->builder, fp, LLVMConstInt(i64_type, 1, false), "its.np");
        LLVMValueRef data_start = LLVMBuildSelect(ctx->builder, is_neg, neg_pos, fp, "its.ds");

        LLVMBasicBlockRef sy = LLVMAppendBasicBlockInContext(ctx->context, current_func, "its.sy");
        LLVMBasicBlockRef sa = LLVMAppendBasicBlockInContext(ctx->context, current_func, "its.sa");
        LLVMMoveBasicBlockAfter(sy, fdn);
        LLVMMoveBasicBlockAfter(sa, sy);
        LLVMBuildCondBr(ctx->builder, is_neg, sy, sa);

        LLVMPositionBuilderAtEnd(ctx->builder, sy);
        LLVMValueRef np2 = LLVMBuildInBoundsGEP2(ctx->builder, i8_type, buf_p, &neg_pos, 1, "its.np");
        LLVMBuildStore(ctx->builder, LLVMConstInt(i8_type, '-', false), np2);
        LLVMBuildBr(ctx->builder, sa);

        LLVMPositionBuilderAtEnd(ctx->builder, sa);
        LLVMValueRef dp = LLVMBuildInBoundsGEP2(ctx->builder, i8_type, buf_p, &data_start, 1, "its.dp");
        TypeDescriptor const * str_desc = get_basic_type_by_name(ctx->type_registry, "string");
        LLVMTypeRef str_type = str_desc ? str_desc->llvm_type : NULL;
        if (str_type == NULL)
        {
            LLVMBuildUnreachable(ctx->builder);
            return;
        }
        LLVMValueRef sv = LLVMGetUndef(str_type);
        sv = LLVMBuildInsertValue(ctx->builder, sv, dp, 0, "its.sd");
        sv = LLVMBuildInsertValue(ctx->builder, sv, total_len, 1, "its.sl");
        LLVMBuildRet(ctx->builder, sv);
    }
    else if (strcmp(func_name, "os_exit") == 0)
    {
        // Parameters: (context_ptr, code: int)
        // SYS_exit = 60 on x86-64
        LLVMValueRef code_val = LLVMGetParam(current_func, 1);

        LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
        LLVMTypeRef code_type = LLVMTypeOf(code_val);
        if (LLVMGetTypeKind(code_type) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(code_type) != 64)
            code_val = LLVMBuildIntCast2(ctx->builder, code_val, i64_type, false, "exit.ext");

        LLVMTypeRef asm_param_types[2] = {i64_type, i64_type};
        LLVMTypeRef asm_ftype = LLVMFunctionType(i64_type, asm_param_types, 2, false);
        LLVMValueRef asm_val = LLVMGetInlineAsm(
            asm_ftype,
            "syscall", 7,
            "={rax},{rax},{rdi},~{rcx},~{r11}", 33,
            true, false, LLVMInlineAsmDialectATT, false
        );
        LLVMValueRef syscall_no = LLVMConstInt(i64_type, 60, false); // SYS_exit
        LLVMValueRef asm_args[2] = {syscall_no, code_val};
        LLVMBuildCall2(ctx->builder, asm_ftype, asm_val, asm_args, 2, "");
        LLVMBuildUnreachable(ctx->builder);
    }
    else if (strcmp(func_name, "sys_write") == 0)
    {
        // Parameters: (context_ptr, fd: int, data: ^u8, count: int)
        LLVMValueRef fd_param = LLVMGetParam(current_func, 1);
        LLVMValueRef data_ptr = LLVMGetParam(current_func, 2);
        LLVMValueRef count_val = LLVMGetParam(current_func, 3);

        // Cast fd to i64 if needed
        LLVMTypeRef fd_type_llvm = LLVMTypeOf(fd_param);
        if (LLVMGetTypeKind(fd_type_llvm) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(fd_type_llvm) != 64)
            fd_param = LLVMBuildIntCast2(ctx->builder, fd_param, i64_type, false, "sw.fd.ext");

        // Cast count to i64 if needed
        LLVMTypeRef count_type = LLVMTypeOf(count_val);
        if (LLVMGetTypeKind(count_type) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(count_type) != 64)
            count_val = LLVMBuildIntCast2(ctx->builder, count_val, i64_type, false, "sw.cnt.ext");

        // Convert data pointer to i64 for syscall
        LLVMValueRef buf_i64 = LLVMBuildPtrToInt(ctx->builder, data_ptr, i64_type, "sw.buf");

        syscall_no = LLVMConstInt(i64_type, 1, false); // SYS_write
        LLVMValueRef sw_asm_args[4] = {syscall_no, fd_param, buf_i64, count_val};
        LLVMValueRef sw_result = LLVMBuildCall2(ctx->builder, asm_ftype, asm_val, sw_asm_args, 4, "sw.result");
        LLVMBuildRet(ctx->builder, sw_result);
    }
    else if (strcmp(func_name, "sys_close") == 0)
    {
        // Parameters: (context_ptr, fd: int)
        // SYS_close = 3, only needs 2 asm args (rax, rdi)
        LLVMValueRef fd_param = LLVMGetParam(current_func, 1);

        LLVMTypeRef fd_type_llvm = LLVMTypeOf(fd_param);
        if (LLVMGetTypeKind(fd_type_llvm) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(fd_type_llvm) != 64)
            fd_param = LLVMBuildIntCast2(ctx->builder, fd_param, i64_type, false, "sc.fd.ext");

        LLVMTypeRef asm_param_types_2[2] = {i64_type, i64_type};
        LLVMTypeRef asm_ftype_2 = LLVMFunctionType(i64_type, asm_param_types_2, 2, false);
        LLVMValueRef asm_val_2 = LLVMGetInlineAsm(
            asm_ftype_2,
            "syscall", 7,
            "={rax},{rax},{rdi},~{rcx},~{r11}", 33,
            true, false, LLVMInlineAsmDialectATT, false
        );
        syscall_no = LLVMConstInt(i64_type, 3, false); // SYS_close
        LLVMValueRef sc_asm_args[2] = {syscall_no, fd_param};
        LLVMValueRef sc_result = LLVMBuildCall2(ctx->builder, asm_ftype_2, asm_val_2, sc_asm_args, 2, "sc.result");
        LLVMBuildRet(ctx->builder, sc_result);
    }
    else if (strcmp(func_name, "sys_open") == 0)
    {
        // Parameters: (context_ptr, path: string, flags: int, mode: int)
        LLVMValueRef path_param = LLVMGetParam(current_func, 1);
        LLVMValueRef flags_param = LLVMGetParam(current_func, 2);
        LLVMValueRef mode_param = LLVMGetParam(current_func, 3);

        // Cast flags and mode to i64 if needed
        LLVMTypeRef flags_type = LLVMTypeOf(flags_param);
        if (LLVMGetTypeKind(flags_type) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(flags_type) != 64)
            flags_param = LLVMBuildIntCast2(ctx->builder, flags_param, i64_type, false, "so.flags.ext");
        LLVMTypeRef mode_type = LLVMTypeOf(mode_param);
        if (LLVMGetTypeKind(mode_type) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(mode_type) != 64)
            mode_param = LLVMBuildIntCast2(ctx->builder, mode_param, i64_type, false, "so.mode.ext");

        // Extract data pointer and length from string struct
        LLVMValueRef path_data = LLVMBuildExtractValue(ctx->builder, path_param, 0, "so.data");
        LLVMValueRef path_len = LLVMBuildExtractValue(ctx->builder, path_param, 1, "so.len");

        // Allocate [4096 x i8] stack buffer for null-terminated cstring copy
        LLVMValueRef buf_a = LLVMBuildAlloca(ctx->builder, LLVMArrayType(i8_type, 4096), "so.buf");
        LLVMValueRef buf_p = LLVMBuildBitCast(ctx->builder, buf_a, LLVMPointerType(i8_type, 0), "so.bp");

        // copy_len = min(path_len, 4095) to leave room for null terminator
        LLVMValueRef max_len = LLVMConstInt(i64_type, 4095, false);
        LLVMValueRef copy_len_gt = LLVMBuildICmp(ctx->builder, LLVMIntUGT, path_len, max_len, "so.cmp");
        LLVMValueRef copy_len = LLVMBuildSelect(ctx->builder, copy_len_gt, max_len, path_len, "so.clen");

        // Memcpy path data to stack buffer (alignment 1 = byte-aligned)
        LLVMBuildMemCpy(ctx->builder, buf_p, 1, path_data, 1, copy_len);

        // buf_p[copy_len] = 0 (null terminator)
        LLVMValueRef null_pos = LLVMBuildInBoundsGEP2(ctx->builder, i8_type, buf_p, &copy_len, 1, "so.nullp");
        LLVMBuildStore(ctx->builder, LLVMConstInt(i8_type, 0, false), null_pos);

        // Convert buffer pointer to i64 for syscall
        LLVMValueRef buf_i64 = LLVMBuildPtrToInt(ctx->builder, buf_p, i64_type, "so.bufi");

        syscall_no = LLVMConstInt(i64_type, 2, false); // SYS_open
        LLVMValueRef so_asm_args[4] = {syscall_no, buf_i64, flags_param, mode_param};
        LLVMValueRef so_result = LLVMBuildCall2(ctx->builder, asm_ftype, asm_val, so_asm_args, 4, "so.result");
        LLVMBuildRet(ctx->builder, so_result);
    }
    else if (strcmp(func_name, "sys_read") == 0)
    {
        // Parameters: (context_ptr, fd: int, data: ^u8, count: int)
        LLVMValueRef fd_param = LLVMGetParam(current_func, 1);
        LLVMValueRef data_ptr = LLVMGetParam(current_func, 2);
        LLVMValueRef count_val = LLVMGetParam(current_func, 3);

        // Cast fd to i64 if needed
        LLVMTypeRef fd_type_llvm = LLVMTypeOf(fd_param);
        if (LLVMGetTypeKind(fd_type_llvm) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(fd_type_llvm) != 64)
            fd_param = LLVMBuildIntCast2(ctx->builder, fd_param, i64_type, false, "sr.fd.ext");

        // Cast count to i64 if needed
        LLVMTypeRef count_type = LLVMTypeOf(count_val);
        if (LLVMGetTypeKind(count_type) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(count_type) != 64)
            count_val = LLVMBuildIntCast2(ctx->builder, count_val, i64_type, false, "sr.cnt.ext");

        // Convert data pointer to i64 for syscall
        LLVMValueRef buf_i64 = LLVMBuildPtrToInt(ctx->builder, data_ptr, i64_type, "sr.buf");

        syscall_no = LLVMConstInt(i64_type, 0, false); // SYS_read
        LLVMValueRef sr_asm_args[4] = {syscall_no, fd_param, buf_i64, count_val};
        LLVMValueRef sr_result = LLVMBuildCall2(ctx->builder, asm_ftype, asm_val, sr_asm_args, 4, "sr.result");
        LLVMBuildRet(ctx->builder, sr_result);
    }
    else
    {
        // Unknown builtin — should have been caught earlier; emit trap just in case
        LLVMBuildUnreachable(ctx->builder);
    }
}

LLVMValueRef
ir_gen_call_malloc(IrGenContext * ctx, LLVMValueRef size)
{
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
    LLVMTypeRef malloc_args[] = {LLVMInt64TypeInContext(ctx->context)};
    LLVMTypeRef malloc_type = LLVMFunctionType(i8ptr, malloc_args, 1, false);
    LLVMValueRef malloc_fn = LLVMGetNamedFunction(ctx->module, "malloc");
    if (malloc_fn == NULL)
        malloc_fn = LLVMAddFunction(ctx->module, "malloc", malloc_type);
    LLVMValueRef args[] = {size};
    return LLVMBuildCall2(ctx->builder, malloc_type, malloc_fn, args, 1, "malloc");
}

void
ir_gen_call_free(IrGenContext * ctx, LLVMValueRef ptr)
{
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
    LLVMTypeRef free_args[] = {i8ptr};
    LLVMTypeRef free_type = LLVMFunctionType(LLVMVoidTypeInContext(ctx->context), free_args, 1, false);
    LLVMValueRef free_fn = LLVMGetNamedFunction(ctx->module, "free");
    if (free_fn == NULL)
        free_fn = LLVMAddFunction(ctx->module, "free", free_type);
    LLVMValueRef args[] = {LLVMBuildPointerCast(ctx->builder, ptr, i8ptr, "")};
    LLVMBuildCall2(ctx->builder, free_type, free_fn, args, 1, "");
}

LLVMValueRef
ir_gen_call_calloc(IrGenContext * ctx, LLVMValueRef size)
{
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
    LLVMTypeRef calloc_args[] = {LLVMInt64TypeInContext(ctx->context), LLVMInt64TypeInContext(ctx->context)};
    LLVMTypeRef calloc_type = LLVMFunctionType(i8ptr, calloc_args, 2, false);
    LLVMValueRef calloc_fn = LLVMGetNamedFunction(ctx->module, "calloc");
    if (calloc_fn == NULL)
        calloc_fn = LLVMAddFunction(ctx->module, "calloc", calloc_type);
    LLVMValueRef one_val = LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 1, false);
    LLVMValueRef args[] = {one_val, size};
    return LLVMBuildCall2(ctx->builder, calloc_type, calloc_fn, args, 2, "calloc");
}

LLVMValueRef
ir_gen_call_strlen(IrGenContext * ctx, LLVMValueRef str_ptr)
{
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0);
    LLVMTypeRef strlen_args[] = {i8ptr};
    LLVMTypeRef strlen_type = LLVMFunctionType(LLVMInt64TypeInContext(ctx->context), strlen_args, 1, false);
    LLVMValueRef strlen_fn = LLVMGetNamedFunction(ctx->module, "strlen");
    if (strlen_fn == NULL)
        strlen_fn = LLVMAddFunction(ctx->module, "strlen", strlen_type);
    LLVMValueRef args[] = {str_ptr};
    return LLVMBuildCall2(ctx->builder, strlen_type, strlen_fn, args, 1, "strlen");
}

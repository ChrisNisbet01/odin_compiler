#include "ir_intrinsic.h"
#include "hash.h"

#include <string.h>
#include <stdint.h>

static generic_hash_table_t * intrinsic_handlers = NULL;

static size_t
hash_intrinsic_name(void const * key)
{
    char const * str = (char const *)key;
    size_t hash = 5381;
    int c;
    while ((c = (unsigned char)*str++) != '\0')
    {
        hash = ((hash << 5) + hash) + (size_t)c;
    }
    return hash;
}

static bool
intrinsic_name_equals(void const * key1, void const * key2)
{
    return strcmp((char const *)key1, (char const *)key2) == 0;
}

static void
init_intrinsic_handlers(void)
{
    if (intrinsic_handlers != NULL)
        return;

    intrinsic_handlers = generic_hash_table_create(16, &(generic_hash_table_key_ops_t){
        .hash = hash_intrinsic_name,
        .equals = intrinsic_name_equals,
    });

    generic_hash_table_insert(intrinsic_handlers, "print_string", (void *)ir_gen_intrinsic_print_string);
    generic_hash_table_insert(intrinsic_handlers, "print_byte", (void *)ir_gen_intrinsic_print_byte);
    generic_hash_table_insert(intrinsic_handlers, "int_to_string", (void *)ir_gen_intrinsic_int_to_string);
    generic_hash_table_insert(intrinsic_handlers, "os_exit", (void *)ir_gen_intrinsic_os_exit);
    generic_hash_table_insert(intrinsic_handlers, "sys_write", (void *)ir_gen_intrinsic_sys_write);
    generic_hash_table_insert(intrinsic_handlers, "sys_close", (void *)ir_gen_intrinsic_sys_close);
    generic_hash_table_insert(intrinsic_handlers, "sys_open", (void *)ir_gen_intrinsic_sys_open);
    generic_hash_table_insert(intrinsic_handlers, "sys_read", (void *)ir_gen_intrinsic_sys_read);
}

LLVMValueRef
ir_gen_intrinsic_print_string(IrGenContext * ctx)
{
    LLVMValueRef fd_param = LLVMGetParam(func_current_function(ctx), 1);
    LLVMValueRef str_param = LLVMGetParam(func_current_function(ctx), 2);

    LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);

    LLVMTypeRef asm_param_types[4] = {i64_type, i64_type, i64_type, i64_type};
    LLVMTypeRef asm_ftype = LLVMFunctionType(i64_type, asm_param_types, 4, false);
    LLVMValueRef asm_val = LLVMGetInlineAsm(
        asm_ftype,
        "syscall", 7,
        "={rax},{rax},{rdi},{rsi},{rdx},~{rcx},~{r11},~{memory}", 54,
        true, false, LLVMInlineAsmDialectATT, false
    );
    LLVMValueRef syscall_no = LLVMConstInt(i64_type, 1, false);

    LLVMTypeRef fd_type_llvm = LLVMTypeOf(fd_param);
    if (LLVMGetTypeKind(fd_type_llvm) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(fd_type_llvm) != 64)
        fd_param = LLVMBuildIntCast2(ctx->builder, fd_param, i64_type, false, "ps.fd.ext");

    LLVMValueRef data_ptr = LLVMBuildExtractValue(ctx->builder, str_param, 0, "ps.data");
    LLVMValueRef len_val = LLVMBuildExtractValue(ctx->builder, str_param, 1, "ps.len");
    LLVMValueRef buf_i64 = LLVMBuildPtrToInt(ctx->builder, data_ptr, i64_type, "ps.buf");

    LLVMValueRef asm_args[4] = {syscall_no, fd_param, buf_i64, len_val};
    LLVMBuildCall2(ctx->builder, asm_ftype, asm_val, asm_args, 4, "");
    LLVMBuildRetVoid(ctx->builder);
    return NULL;
}

LLVMValueRef
ir_gen_intrinsic_print_byte(IrGenContext * ctx)
{
    LLVMValueRef fd_param = LLVMGetParam(func_current_function(ctx), 1);
    LLVMValueRef byte_val = LLVMGetParam(func_current_function(ctx), 2);

    LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef i8_type = LLVMInt8TypeInContext(ctx->context);

    LLVMTypeRef asm_param_types[4] = {i64_type, i64_type, i64_type, i64_type};
    LLVMTypeRef asm_ftype = LLVMFunctionType(i64_type, asm_param_types, 4, false);
    LLVMValueRef asm_val = LLVMGetInlineAsm(
        asm_ftype,
        "syscall", 7,
        "={rax},{rax},{rdi},{rsi},{rdx},~{rcx},~{r11}", 54,
        true, false, LLVMInlineAsmDialectATT, false
    );
    LLVMValueRef syscall_no = LLVMConstInt(i64_type, 1, false);

    LLVMTypeRef fd_type_llvm = LLVMTypeOf(fd_param);
    if (LLVMGetTypeKind(fd_type_llvm) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(fd_type_llvm) != 64)
        fd_param = LLVMBuildIntCast2(ctx->builder, fd_param, i64_type, false, "pb.fd.ext");

    if (LLVMGetIntTypeWidth(LLVMTypeOf(byte_val)) != 8)
        byte_val = LLVMBuildTrunc(ctx->builder, byte_val, i8_type, "pb.trunc");

    LLVMValueRef byte_alloca = LLVMBuildAlloca(ctx->builder, i8_type, "pb.byte");
    LLVMBuildStore(ctx->builder, byte_val, byte_alloca);
    LLVMValueRef buf_i64 = LLVMBuildPtrToInt(ctx->builder, byte_alloca, i64_type, "pb.buf");

    LLVMValueRef asm_args[4] = {syscall_no, fd_param, buf_i64, LLVMConstInt(i64_type, 1, false)};
    LLVMBuildCall2(ctx->builder, asm_ftype, asm_val, asm_args, 4, "");
    LLVMBuildRetVoid(ctx->builder);
    return NULL;
}

LLVMValueRef
ir_gen_intrinsic_int_to_string(IrGenContext * ctx)
{
    LLVMValueRef i64_val = LLVMGetParam(func_current_function(ctx), 1);

    LLVMTypeRef val_type = LLVMTypeOf(i64_val);
    if (LLVMGetTypeKind(val_type) != LLVMIntegerTypeKind)
    {
        LLVMBuildUnreachable(ctx->builder);
        return NULL;
    }

    LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef i8_type = LLVMInt8TypeInContext(ctx->context);

    if (LLVMGetIntTypeWidth(val_type) < 64)
        i64_val = LLVMBuildSExt(ctx->builder, i64_val, i64_type, "its.ext");
    else if (LLVMGetIntTypeWidth(val_type) > 64)
        i64_val = LLVMBuildTrunc(ctx->builder, i64_val, i64_type, "its.trunc");

    LLVMValueRef zero = LLVMConstInt(i64_type, 0, false);

    LLVMValueRef is_neg = LLVMBuildICmp(ctx->builder, LLVMIntSLT, i64_val, zero, "its.isneg");
    LLVMValueRef neg_val = LLVMBuildSub(ctx->builder, zero, i64_val, "its.neg");
    LLVMValueRef abs_val = LLVMBuildSelect(ctx->builder, is_neg, neg_val, i64_val, "its.abs");

    LLVMValueRef abs_saved = LLVMBuildAlloca(ctx->builder, i64_type, "its.abs.saved");
    LLVMBuildStore(ctx->builder, abs_val, abs_saved);
    LLVMValueRef n_digits_a = LLVMBuildAlloca(ctx->builder, i64_type, "its.ndigits");
    LLVMBuildStore(ctx->builder, zero, n_digits_a);
    LLVMValueRef temp_a = LLVMBuildAlloca(ctx->builder, i64_type, "its.temp");
    LLVMBuildStore(ctx->builder, abs_val, temp_a);

    LLVMBasicBlockRef ck_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "its.ck");
    LLVMBasicBlockRef cb_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "its.cb");
    LLVMBasicBlockRef cd_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "its.cd");
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

    LLVMBasicBlockRef fck = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "its.fck");
    LLVMBasicBlockRef fbd = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "its.fbd");
    LLVMBasicBlockRef fdn = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "its.fdn");
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

    LLVMBasicBlockRef sy = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "its.sy");
    LLVMBasicBlockRef sa = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "its.sa");
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
        return NULL;
    }
    LLVMValueRef sv = LLVMGetUndef(str_type);
    sv = LLVMBuildInsertValue(ctx->builder, sv, dp, 0, "its.sd");
    sv = LLVMBuildInsertValue(ctx->builder, sv, total_len, 1, "its.sl");
    LLVMBuildRet(ctx->builder, sv);
    return NULL;
}

LLVMValueRef
ir_gen_intrinsic_os_exit(IrGenContext * ctx)
{
    LLVMValueRef code_val = LLVMGetParam(func_current_function(ctx), 1);

    LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef code_type = LLVMTypeOf(code_val);
    if (LLVMGetTypeKind(code_type) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(code_type) != 64)
        code_val = LLVMBuildIntCast2(ctx->builder, code_val, i64_type, false, "exit.ext");

    LLVMTypeRef asm_param_types[2] = {i64_type, i64_type};
    LLVMTypeRef asm_ftype = LLVMFunctionType(i64_type, asm_param_types, 2, false);
    LLVMValueRef asm_val = LLVMGetInlineAsm(
        asm_ftype,
        "syscall", 7,
        "={rax},{rax},{rdi},~{rcx},~{r11}", 32,
        true, false, LLVMInlineAsmDialectATT, false
    );
    LLVMValueRef syscall_no = LLVMConstInt(i64_type, 60, false);
    LLVMValueRef asm_args[2] = {syscall_no, code_val};
    LLVMBuildCall2(ctx->builder, asm_ftype, asm_val, asm_args, 2, "");
    LLVMBuildUnreachable(ctx->builder);
    return NULL;
}

LLVMValueRef
ir_gen_intrinsic_sys_write(IrGenContext * ctx)
{
    LLVMValueRef fd_param = LLVMGetParam(func_current_function(ctx), 1);
    LLVMValueRef data_ptr = LLVMGetParam(func_current_function(ctx), 2);
    LLVMValueRef count_val = LLVMGetParam(func_current_function(ctx), 3);

    LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);

    LLVMTypeRef fd_type_llvm = LLVMTypeOf(fd_param);
    if (LLVMGetTypeKind(fd_type_llvm) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(fd_type_llvm) != 64)
        fd_param = LLVMBuildIntCast2(ctx->builder, fd_param, i64_type, false, "sw.fd.ext");

    LLVMTypeRef count_type = LLVMTypeOf(count_val);
    if (LLVMGetTypeKind(count_type) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(count_type) != 64)
        count_val = LLVMBuildIntCast2(ctx->builder, count_val, i64_type, false, "sw.cnt.ext");

    LLVMValueRef buf_i64 = LLVMBuildPtrToInt(ctx->builder, data_ptr, i64_type, "sw.buf");

    LLVMTypeRef asm_param_types[4] = {i64_type, i64_type, i64_type, i64_type};
    LLVMTypeRef asm_ftype = LLVMFunctionType(i64_type, asm_param_types, 4, false);
    LLVMValueRef asm_val = LLVMGetInlineAsm(
        asm_ftype,
        "syscall", 7,
        "={rax},{rax},{rdi},{rsi},{rdx},~{rcx},~{r11},~{memory}", 54,
        true, false, LLVMInlineAsmDialectATT, false
    );
    LLVMValueRef syscall_no = LLVMConstInt(i64_type, 1, false);
    LLVMValueRef sw_asm_args[4] = {syscall_no, fd_param, buf_i64, count_val};
    LLVMValueRef sw_result = LLVMBuildCall2(ctx->builder, asm_ftype, asm_val, sw_asm_args, 4, "sw.result");
    LLVMBuildRet(ctx->builder, sw_result);
    return NULL;
}

LLVMValueRef
ir_gen_intrinsic_sys_close(IrGenContext * ctx)
{
    LLVMValueRef fd_param = LLVMGetParam(func_current_function(ctx), 1);

    LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);
    LLVMTypeRef fd_type_llvm = LLVMTypeOf(fd_param);
    if (LLVMGetTypeKind(fd_type_llvm) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(fd_type_llvm) != 64)
        fd_param = LLVMBuildIntCast2(ctx->builder, fd_param, i64_type, false, "sc.fd.ext");

    LLVMTypeRef asm_param_types[2] = {i64_type, i64_type};
    LLVMTypeRef asm_ftype = LLVMFunctionType(i64_type, asm_param_types, 2, false);
    LLVMValueRef asm_val = LLVMGetInlineAsm(
        asm_ftype,
        "syscall", 7,
        "={rax},{rax},{rdi},~{rcx},~{r11}", 32,
        true, false, LLVMInlineAsmDialectATT, false
    );
    LLVMValueRef syscall_no = LLVMConstInt(i64_type, 3, false);
    LLVMValueRef sc_asm_args[2] = {syscall_no, fd_param};
    LLVMValueRef sc_result = LLVMBuildCall2(ctx->builder, asm_ftype, asm_val, sc_asm_args, 2, "sc.result");
    LLVMBuildRet(ctx->builder, sc_result);
    return NULL;
}

LLVMValueRef
ir_gen_intrinsic_sys_open(IrGenContext * ctx)
{
    LLVMValueRef path_param = LLVMGetParam(func_current_function(ctx), 1);
    LLVMValueRef flags_param = LLVMGetParam(func_current_function(ctx), 2);
    LLVMValueRef mode_param = LLVMGetParam(func_current_function(ctx), 3);

    LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);

    LLVMTypeRef flags_type = LLVMTypeOf(flags_param);
    if (LLVMGetTypeKind(flags_type) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(flags_type) != 64)
        flags_param = LLVMBuildIntCast2(ctx->builder, flags_param, i64_type, false, "so.flags.ext");
    LLVMTypeRef mode_type = LLVMTypeOf(mode_param);
    if (LLVMGetTypeKind(mode_type) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(mode_type) != 64)
        mode_param = LLVMBuildIntCast2(ctx->builder, mode_param, i64_type, false, "so.mode.ext");

    LLVMValueRef path_data = LLVMBuildExtractValue(ctx->builder, path_param, 0, "so.data");
    LLVMValueRef path_len = LLVMBuildExtractValue(ctx->builder, path_param, 1, "so.len");

    LLVMTypeRef i8_type = LLVMInt8TypeInContext(ctx->context);
    LLVMValueRef buf_a = LLVMBuildAlloca(ctx->builder, LLVMArrayType(i8_type, 4096), "so.buf");
    LLVMValueRef buf_p = LLVMBuildBitCast(ctx->builder, buf_a, LLVMPointerType(i8_type, 0), "so.bp");

    LLVMValueRef max_len = LLVMConstInt(i64_type, 4095, false);
    LLVMValueRef copy_len_gt = LLVMBuildICmp(ctx->builder, LLVMIntUGT, path_len, max_len, "so.cmp");
    LLVMValueRef copy_len = LLVMBuildSelect(ctx->builder, copy_len_gt, max_len, path_len, "so.clen");

    LLVMBuildMemCpy(ctx->builder, buf_p, 1, path_data, 1, copy_len);

    LLVMValueRef null_pos = LLVMBuildInBoundsGEP2(ctx->builder, i8_type, buf_p, &copy_len, 1, "so.nullp");
    LLVMBuildStore(ctx->builder, LLVMConstInt(i8_type, 0, false), null_pos);

    LLVMValueRef buf_i64 = LLVMBuildPtrToInt(ctx->builder, buf_p, i64_type, "so.bufi");

    LLVMTypeRef asm_param_types[4] = {i64_type, i64_type, i64_type, i64_type};
    LLVMTypeRef asm_ftype = LLVMFunctionType(i64_type, asm_param_types, 4, false);
    LLVMValueRef asm_val = LLVMGetInlineAsm(
        asm_ftype,
        "syscall", 7,
        "={rax},{rax},{rdi},{rsi},{rdx},~{rcx},~{r11},~{memory}", 54,
        true, false, LLVMInlineAsmDialectATT, false
    );
    LLVMValueRef syscall_no = LLVMConstInt(i64_type, 2, false);
    LLVMValueRef so_asm_args[4] = {syscall_no, buf_i64, flags_param, mode_param};
    LLVMValueRef so_result = LLVMBuildCall2(ctx->builder, asm_ftype, asm_val, so_asm_args, 4, "so.result");
    LLVMBuildRet(ctx->builder, so_result);
    return NULL;
}

LLVMValueRef
ir_gen_intrinsic_sys_read(IrGenContext * ctx)
{
    LLVMValueRef fd_param = LLVMGetParam(func_current_function(ctx), 1);
    LLVMValueRef data_ptr = LLVMGetParam(func_current_function(ctx), 2);
    LLVMValueRef count_val = LLVMGetParam(func_current_function(ctx), 3);

    LLVMTypeRef i64_type = LLVMInt64TypeInContext(ctx->context);

    LLVMTypeRef fd_type_llvm = LLVMTypeOf(fd_param);
    if (LLVMGetTypeKind(fd_type_llvm) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(fd_type_llvm) != 64)
        fd_param = LLVMBuildIntCast2(ctx->builder, fd_param, i64_type, false, "sr.fd.ext");

    LLVMTypeRef count_type = LLVMTypeOf(count_val);
    if (LLVMGetTypeKind(count_type) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(count_type) != 64)
        count_val = LLVMBuildIntCast2(ctx->builder, count_val, i64_type, false, "sr.cnt.ext");

    LLVMValueRef buf_i64 = LLVMBuildPtrToInt(ctx->builder, data_ptr, i64_type, "sr.buf");

    LLVMTypeRef asm_param_types[4] = {i64_type, i64_type, i64_type, i64_type};
    LLVMTypeRef asm_ftype = LLVMFunctionType(i64_type, asm_param_types, 4, false);
    LLVMValueRef asm_val = LLVMGetInlineAsm(
        asm_ftype,
        "syscall", 7,
        "={rax},{rax},{rdi},{rsi},{rdx},~{rcx},~{r11},~{memory}", 54,
        true, false, LLVMInlineAsmDialectATT, false
    );
    LLVMValueRef syscall_no = LLVMConstInt(i64_type, 0, false);
    LLVMValueRef sr_asm_args[4] = {syscall_no, fd_param, buf_i64, count_val};
    LLVMValueRef sr_result = LLVMBuildCall2(ctx->builder, asm_ftype, asm_val, sr_asm_args, 4, "sr.result");
    LLVMBuildRet(ctx->builder, sr_result);
    return NULL;
}

void
ir_gen_runtime_intrinsic_body(IrGenContext * ctx, char const * func_name,
                                TypeDescriptor const * proc_type)
{
    (void)proc_type;

    if (intrinsic_handlers == NULL)
        init_intrinsic_handlers();

    intrinsic_handler_fn handler = (intrinsic_handler_fn)generic_hash_table_lookup(intrinsic_handlers, func_name);
    if (handler != NULL)
    {
        handler(ctx, func_name, proc_type);
        return;
    }

    LLVMBuildUnreachable(ctx->builder);
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
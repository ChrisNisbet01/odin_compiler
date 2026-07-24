#include "ir_gen_statement.h"

// --- Defer helper functions ---

void
ir_gen_emit_defers_at_depth(IrGenContext * ctx, int depth)
{
    // Emit defers at given scope depth from top to bottom (LIFO)
    for (int i = ctx->defer_count - 1; i >= 0; i--)
    {
        if (ctx->defer_stack[i].scope_depth == depth)
        {
            ir_gen_node(ctx, ctx->defer_stack[i].node);
        }
    }
    // Compact stack, removing entries at this depth
    int write = 0;
    for (int read = 0; read < ctx->defer_count; read++)
    {
        if (ctx->defer_stack[read].scope_depth != depth)
        {
            ctx->defer_stack[write++] = ctx->defer_stack[read];
        }
    }
    ctx->defer_count = write;
}

void
ir_gen_emit_defers_from_depth(IrGenContext * ctx, int min_depth)
{
    // Emit all defers with scope_depth >= min_depth (inside the loop/switch body)
    // Iterate from top to bottom so inner defers fire first (LIFO)
    for (int i = ctx->defer_count - 1; i >= 0; i--)
    {
        if (ctx->defer_stack[i].scope_depth >= min_depth)
        {
            ir_gen_node(ctx, ctx->defer_stack[i].node);
        }
    }
    // Compact stack, keeping only defers with scope_depth < min_depth
    int write = 0;
    for (int read = 0; read < ctx->defer_count; read++)
    {
        if (ctx->defer_stack[read].scope_depth < min_depth)
        {
            ctx->defer_stack[write++] = ctx->defer_stack[read];
        }
    }
    ctx->defer_count = write;
}

void
ir_gen_emit_all_defers(IrGenContext * ctx)
{
    // Emit all pending defers from top to bottom (LIFO, inner scope first)
    for (int i = ctx->defer_count - 1; i >= 0; i--)
    {
        ir_gen_node(ctx, ctx->defer_stack[i].node);
    }
    ctx->defer_count = 0;
}

bool
ir_gen_node_contains_auto_cast(odin_grammar_node_t * node)
{
    if (node == NULL)
        return false;
    if (node->type == AST_NODE_AUTO_CAST_EXPR)
        return true;
    for (size_t ci = 0; ci < node->list.count; ci++)
    {
        if (ir_gen_node_contains_auto_cast(node->list.children[ci]))
            return true;
    }
    return false;
}

// --- Statement codegen ---

LLVMValueRef
ir_gen_return_statement(IrGenContext * ctx, odin_grammar_node_t * node)
{
    // Collect all expression children (may be multiple for multi-return)
    int expr_count = 0;
    odin_grammar_node_t * exprs[16];
    for (size_t i = 0; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child != NULL && expr_count < 16)
        {
            exprs[expr_count++] = child;
        }
    }

    // Evaluate return values first (so defers can be emitted after)
    LLVMValueRef vals[16];
    LLVMValueRef func_val = func_current_function(ctx);
    LLVMTypeRef func_llvm_type = LLVMGlobalGetValueType(func_val);
    LLVMTypeRef ret_llvm_type = LLVMGetReturnType(func_llvm_type);

    for (int i = 0; i < expr_count; i++)
    {
        if (expr_count == 1 && ir_gen_node_contains_auto_cast(exprs[i]))
        {
            ctx->auto_cast_target_type = ret_llvm_type;
        }
        vals[i] = ir_gen_node(ctx, exprs[i]);
        ctx->auto_cast_target_type = NULL;
        if (vals[i] == NULL && expr_count > 0)
            return NULL;

        // If the value is an alloca pointer to a composite type, load it.
        // ir_gen_identifier returns alloca pointers for composites (needed
        // for GEP/subscript/member), but return values need the actual value.
        if (vals[i] != NULL
            && exprs[i]->resolved_type != NULL
            && LLVMGetTypeKind(LLVMTypeOf(vals[i])) == LLVMPointerTypeKind
            && exprs[i]->resolved_type->llvm_type != NULL
            && LLVMTypeOf(vals[i]) != exprs[i]->resolved_type->llvm_type)
        {
            vals[i] = LLVMBuildLoad2(ctx->builder, exprs[i]->resolved_type->llvm_type,
                                      vals[i], "ret.load");
        }

        // Coerce return value to match expected function return type
        if (expr_count == 1 && vals[i] != NULL && LLVMTypeOf(vals[i]) != ret_llvm_type)
        {
            vals[i] = coerce_value_to_type(ctx, vals[i], ret_llvm_type, false, "retcast");
        }
    }

    // Emit all pending defers before the return instruction
    ir_gen_emit_all_defers(ctx);

    if (expr_count == 0)
    {
        return LLVMBuildRetVoid(ctx->builder);
    }

    if (expr_count == 1)
    {
        return LLVMBuildRet(ctx->builder, vals[0]);
    }

    // Multi-return: pack values into struct
    LLVMValueRef func = func_current_function(ctx);
    LLVMTypeRef func_type = LLVMGlobalGetValueType(func);
    LLVMTypeRef ret_struct = LLVMGetReturnType(func_type);

    LLVMValueRef struct_val = LLVMGetUndef(ret_struct);
    for (int i = 0; i < expr_count; i++)
    {
        struct_val = LLVMBuildInsertValue(ctx->builder, struct_val, vals[i], (unsigned)i, "ret.field");
    }
    return LLVMBuildRet(ctx->builder, struct_val);
}

LLVMValueRef
ir_gen_compound_statement(IrGenContext * ctx, odin_grammar_node_t * node)
{
    for (size_t i = 0; i < node->list.count; i++)
    {
        ir_gen_node(ctx, node->list.children[i]);
    }
    return NULL;
}

// --- Control flow codegen ---

LLVMValueRef
ir_gen_if_statement(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 1)
        return NULL;

    odin_grammar_node_t * cond_node = node->list.children[0];
    odin_grammar_node_t * then_node = (node->list.count > 1) ? node->list.children[1] : NULL;
    odin_grammar_node_t * else_node = (node->list.count > 2) ? node->list.children[2] : NULL;

    if (cond_node == NULL)
        return NULL;

    LLVMValueRef cond_val = ir_gen_node(ctx, cond_node);
    if (cond_val == NULL)
        return NULL;

    LLVMTypeRef cond_type = LLVMTypeOf(cond_val);
    LLVMTypeKind cond_kind = LLVMGetTypeKind(cond_type);

    LLVMValueRef bool_cond;
    if (cond_kind == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(cond_type) == 1)
    {
        bool_cond = cond_val;
    }
    else
    {
        LLVMValueRef zero = LLVMConstNull(cond_type);
        bool_cond = LLVMBuildICmp(ctx->builder, LLVMIntNE, cond_val, zero, "ifcond");
    }

    LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "then");
    LLVMBasicBlockRef else_bb
        = else_node ? LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "else") : NULL;
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "ifmerge");

    if (else_bb)
    {
        LLVMBuildCondBr(ctx->builder, bool_cond, then_bb, else_bb);
    }
    else
    {
        LLVMBuildCondBr(ctx->builder, bool_cond, then_bb, merge_bb);
    }

    // Then block
    LLVMPositionBuilderAtEnd(ctx->builder, then_bb);
    if (then_node)
    {
        ir_gen_node(ctx, then_node);
    }
    LLVMBasicBlockRef then_end = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef then_term = LLVMGetLastInstruction(then_end);
    if (then_term == NULL || !LLVMIsATerminatorInst(then_term))
    {
        LLVMBuildBr(ctx->builder, merge_bb);
    }

    // Else block
    if (else_bb)
    {
        LLVMPositionBuilderAtEnd(ctx->builder, else_bb);
        if (else_node)
        {
            ir_gen_node(ctx, else_node);
        }
        LLVMBasicBlockRef else_end = LLVMGetInsertBlock(ctx->builder);
        LLVMValueRef else_term = LLVMGetLastInstruction(else_end);
        if (else_term == NULL || !LLVMIsATerminatorInst(else_term))
        {
            LLVMBuildBr(ctx->builder, merge_bb);
        }
    }

    LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
    return NULL;
}

LLVMValueRef
ir_gen_for_statement(IrGenContext * ctx, odin_grammar_node_t * node)
{
    if (node->list.count < 1)
        return NULL;

    odin_grammar_node_t * cond_node = NULL;
    odin_grammar_node_t * body_node = NULL;

    // Detect for-range: first child is a raw Identifier
    bool is_for_range = false;
    odin_grammar_node_t * loop_var_nodes[MAX_LOOP_DEPTH];
    int loop_var_count = 0;
    odin_grammar_node_t * range_expr_node = NULL;

    if (node->list.count >= 2 && node->list.children[0] != NULL && node->list.children[0]->type == AST_NODE_IDENTIFIER)
    {
        for (size_t i = 0; i < node->list.count && loop_var_count < MAX_LOOP_DEPTH; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_COMPOUND_STATEMENT)
                break;
            if (child->type == AST_NODE_IDENTIFIER)
            {
                loop_var_nodes[loop_var_count++] = child;
                continue;
            }
            if (child->resolved_type && child->resolved_type->kind == TD_KIND_RANGE)
            {
                is_for_range = true;
                range_expr_node = child;
                continue;
            }
            // Non-identifier, non-range child — this is the do-form body
            break;
        }
    }

    if (is_for_range)
    {
        LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->context);
        LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx->context);
        LLVMValueRef zero_i32 = LLVMConstInt(i32, 0, false);
        LLVMValueRef one_i32 = LLVMConstInt(i32, 1, false);

        // 1. Evaluate range expression → produces {i64, i64} struct value
        LLVMValueRef range_val = ir_gen_node(ctx, range_expr_node);
        if (!range_val)
        {
            LLVMBuildUnreachable(ctx->builder);
            return NULL;
        }

        // 2. Extract low (idx 0) and high (idx 1) from the struct
        LLVMTypeRef range_struct = LLVMTypeOf(range_val);
        LLVMValueRef range_alloca = LLVMBuildAlloca(ctx->builder, range_struct, "for.range");
        LLVMBuildStore(ctx->builder, range_val, range_alloca);

        LLVMValueRef low_gep_i[2] = {zero_i32, zero_i32};
        LLVMValueRef low_gep
            = LLVMBuildInBoundsGEP2(ctx->builder, range_struct, range_alloca, low_gep_i, 2, "for.range.low.gep");
        LLVMValueRef low_val = LLVMBuildLoad2(ctx->builder, i64, low_gep, "for.low");

        LLVMValueRef high_gep_i[2] = {zero_i32, one_i32};
        LLVMValueRef high_gep
            = LLVMBuildInBoundsGEP2(ctx->builder, range_struct, range_alloca, high_gep_i, 2, "for.range.high.gep");
        LLVMValueRef high_val = LLVMBuildLoad2(ctx->builder, i64, high_gep, "for.high");

        // 3. Find body node (CompoundStatement or do-form statement)
        for (size_t i = 0; i < node->list.count; i++)
        {
            odin_grammar_node_t * child = node->list.children[i];
            if (child == NULL)
                continue;
            if (child->type == AST_NODE_COMPOUND_STATEMENT)
            {
                body_node = child;
                break;
            }
            // For do-form: body is the last child that is not an identifier, range, or compound
            if (child->type != AST_NODE_IDENTIFIER && !(child->resolved_type && child->resolved_type->kind == TD_KIND_RANGE))
            {
                body_node = child;
            }
        }

        // 4. Build loop blocks
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "forcond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "forbody");
        LLVMBasicBlockRef inc_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "forinc");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "forend");

        // Push loop context (continue goes to inc block)
        if (ctx->loop_depth < MAX_LOOP_DEPTH)
        {
            ctx->loop_stack[ctx->loop_depth].continue_bb = inc_bb;
            ctx->loop_stack[ctx->loop_depth].break_bb = end_bb;
            ctx->loop_stack[ctx->loop_depth].scope_depth = ctx->current_scope_depth;
            ctx->loop_depth++;
        }

        // Allocate loop variables, initialize to low, register in scope
        LLVMValueRef loop_allocas[MAX_LOOP_DEPTH];
        TypeDescriptor const * i64_td = type_descriptor_get_int64_type(ctx->type_registry);
        for (int vi = 0; vi < loop_var_count; vi++)
        {
            char const * name = loop_var_nodes[vi] ? loop_var_nodes[vi]->text : "for.var";
            loop_allocas[vi] = LLVMBuildAlloca(ctx->builder, i64, name);
            LLVMSetAlignment(loop_allocas[vi], LLVMABIAlignmentOfType(ctx->data_layout, i64));
            LLVMBuildStore(ctx->builder, low_val, loop_allocas[vi]);
            TypedValue tv = create_typed_value(loop_allocas[vi], i64_td, true);
            generator_add_symbol(ctx->gen_ctx, name, tv);
        }

        LLVMBuildBr(ctx->builder, cond_bb);

        // Condition block: loop_var < high
        LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
        LLVMValueRef loop_val = LLVMBuildLoad2(ctx->builder, i64, loop_allocas[0], "for.i.val");
        LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntSLT, loop_val, high_val, "for.cmp");
        LLVMBuildCondBr(ctx->builder, cmp, body_bb, end_bb);

        // Body block
        LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
        if (body_node)
        {
            ir_gen_node(ctx, body_node);
        }
        LLVMBasicBlockRef body_end_bb = LLVMGetInsertBlock(ctx->builder);
        LLVMValueRef body_term = LLVMGetLastInstruction(body_end_bb);
        if (body_term == NULL || !LLVMIsATerminatorInst(body_term))
        {
            LLVMBuildBr(ctx->builder, inc_bb);
        }

        // Increment block: load first var, increment, store to all vars
        LLVMPositionBuilderAtEnd(ctx->builder, inc_bb);
        LLVMValueRef old_val = LLVMBuildLoad2(ctx->builder, i64, loop_allocas[0], "for.i.old");
        LLVMValueRef inc = LLVMBuildAdd(ctx->builder, old_val, LLVMConstInt(i64, 1, false), "for.i.inc");
        for (int vi = 0; vi < loop_var_count; vi++)
        {
            LLVMBuildStore(ctx->builder, inc, loop_allocas[vi]);
        }
        LLVMBuildBr(ctx->builder, cond_bb);

        // Pop loop context
        if (ctx->loop_depth > 0)
            ctx->loop_depth--;

        LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
        return NULL;
    }

    // Original for-loop logic
    for (size_t i = 0; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child == NULL)
            continue;
        if (child->type == AST_NODE_COMPOUND_STATEMENT)
        {
            body_node = child;
        }
        else
        {
            cond_node = child;
        }
    }

    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "forcond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "forbody");
    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "forend");

    // Push loop context for break/continue
    if (ctx->loop_depth < MAX_LOOP_DEPTH)
    {
        ctx->loop_stack[ctx->loop_depth].continue_bb = cond_bb;
        ctx->loop_stack[ctx->loop_depth].break_bb = end_bb;
        ctx->loop_stack[ctx->loop_depth].scope_depth = ctx->current_scope_depth;
        ctx->loop_depth++;
    }

    LLVMBuildBr(ctx->builder, cond_bb);

    // Condition block
    LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
    if (cond_node)
    {
        LLVMValueRef cond_val = ir_gen_node(ctx, cond_node);
        if (cond_val)
        {
            LLVMTypeRef cond_type = LLVMTypeOf(cond_val);
            LLVMTypeKind cond_kind = LLVMGetTypeKind(cond_type);
            LLVMValueRef bool_cond;
            if (cond_kind == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(cond_type) == 1)
            {
                bool_cond = cond_val;
            }
            else
            {
                LLVMValueRef zero = LLVMConstNull(cond_type);
                bool_cond = LLVMBuildICmp(ctx->builder, LLVMIntNE, cond_val, zero, "forcond");
            }
            LLVMBuildCondBr(ctx->builder, bool_cond, body_bb, end_bb);
        }
        else
        {
            LLVMBuildBr(ctx->builder, body_bb);
        }
    }
    else
    {
        LLVMBuildBr(ctx->builder, body_bb);
    }

    // Body block
    LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
    if (body_node)
    {
        ir_gen_node(ctx, body_node);
    }
    LLVMBasicBlockRef body_end = LLVMGetInsertBlock(ctx->builder);
    LLVMValueRef body_term = LLVMGetLastInstruction(body_end);
    if (body_term == NULL || !LLVMIsATerminatorInst(body_term))
    {
        LLVMBuildBr(ctx->builder, cond_bb);
    }

    // Pop loop context
    if (ctx->loop_depth > 0)
        ctx->loop_depth--;

    LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
    return NULL;
}

LLVMValueRef
ir_gen_switch_statement(IrGenContext * ctx, odin_grammar_node_t * node)
{
    // SwitchStatement = KwSwitch Directive? Expression? SwitchBody
    // Children: Expression (optional), SwitchCaseClause(s), SwitchDefaultClause
    // (optional)
    odin_grammar_node_t * switch_expr = NULL;
    odin_grammar_node_t * default_case = NULL;
    odin_grammar_node_t * case_clauses[64];
    int case_count = 0;

    // Separate children into expression, cases, and default
    for (size_t i = 0; i < node->list.count; i++)
    {
        odin_grammar_node_t * child = node->list.children[i];
        if (child == NULL)
            continue;
        if (child->type == AST_NODE_SWITCH_CASE)
        {
            if (case_count < 64)
                case_clauses[case_count++] = child;
        }
        else if (child->type == AST_NODE_SWITCH_DEFAULT)
        {
            default_case = child;
        }
        else if (child->type == AST_NODE_DIRECTIVE || child->type == AST_NODE_DIRECTIVE_WITH_ARGS)
        {
            // Skip directives
        }
        else
        {
            switch_expr = child;
        }
    }

    if (switch_expr == NULL)
        return NULL;

    LLVMValueRef switch_val = ir_gen_node(ctx, switch_expr);
    if (switch_val == NULL)
        return NULL;

    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "swend");

    // Push loop context for break in switch
    if (ctx->loop_depth < MAX_LOOP_DEPTH)
    {
        ctx->loop_stack[ctx->loop_depth].continue_bb = NULL;
        ctx->loop_stack[ctx->loop_depth].break_bb = end_bb;
        ctx->loop_stack[ctx->loop_depth].scope_depth = ctx->current_scope_depth;
        ctx->loop_depth++;
    }

    // Create a block for each case + a default block
    LLVMBasicBlockRef case_bbs[64];
    LLVMBasicBlockRef default_bb = NULL;

    for (int i = 0; i < case_count; i++)
    {
        case_bbs[i] = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "case");
    }
    if (default_case)
    {
        default_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "default");
    }

    // Build compare-and-branch chain (like if-else)
    // Each case checks its value and branches to its body; next case on mismatch
    for (int i = 0; i < case_count; i++)
    {
        odin_grammar_node_t * case_clause = case_clauses[i];
        // First child of SwitchCaseClause is KwCase (terminal, no child),
        // then Expression(s) (could be multiple comma-separated values),
        // then Statement*s.
        // The case clause body is embedded in this node (not a separate compound
        // stmt).

        if (case_clause->list.count < 1)
            continue;

        odin_grammar_node_t * case_val_node = NULL;
        // Find the first expression child for the case value
        for (size_t ci = 0; ci < case_clause->list.count; ci++)
        {
            odin_grammar_node_t * child = case_clause->list.children[ci];
            if (child == NULL)
                continue;
            if (child->type != AST_NODE_COMPOUND_STATEMENT && child->type != AST_NODE_RETURN_STATEMENT
                && child->type != AST_NODE_BREAK_STATEMENT && child->type != AST_NODE_CONTINUE_STATEMENT
                && child->type != AST_NODE_EXPRESSION_STATEMENT && child->type != AST_NODE_ASSIGN_STATEMENT
                && child->type != AST_NODE_VARIABLE_DECL && child->type != AST_NODE_IF_STATEMENT
                && child->type != AST_NODE_FOR_STATEMENT && child->type != AST_NODE_SWITCH_STATEMENT)
            {
                case_val_node = child;
                break;
            }
        }

        if (case_val_node == NULL)
            continue;

        LLVMValueRef case_val = ir_gen_node(ctx, case_val_node);
        if (case_val == NULL)
            continue;

        LLVMBasicBlockRef case_bb = case_bbs[i];
        LLVMBasicBlockRef miss_bb;
        if (i < case_count - 1)
            miss_bb = LLVMAppendBasicBlockInContext(ctx->context, func_current_function(ctx), "swnext");
        else if (default_bb)
            miss_bb = default_bb;
        else
            miss_bb = end_bb;

        LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntEQ, switch_val, case_val, "swcmp");
        LLVMBuildCondBr(ctx->builder, cmp, case_bb, miss_bb);

        // Case body
        LLVMPositionBuilderAtEnd(ctx->builder, case_bb);

        // Set fallthrough target for any fallthrough statement in this case
        if (i < case_count - 1)
            ctx->fallthrough_target_bb = case_bbs[i + 1];
        else if (default_bb)
            ctx->fallthrough_target_bb = default_bb;
        else
            ctx->fallthrough_target_bb = end_bb;

        ctx->current_scope_depth++;
        generator_push_scope(ctx->gen_ctx);
        for (size_t ci = 1; ci < case_clause->list.count; ci++)
        {
            odin_grammar_node_t * stmt = case_clause->list.children[ci];
            if (stmt == NULL)
                continue;
            if (stmt == case_val_node)
                continue;
            ir_gen_node(ctx, stmt);
        }
        LLVMBasicBlockRef case_block_end = LLVMGetInsertBlock(ctx->builder);
        LLVMValueRef case_term = LLVMGetLastInstruction(case_block_end);
        if (case_term == NULL || !LLVMIsATerminatorInst(case_term))
        {
            ir_gen_emit_defers_at_depth(ctx, ctx->current_scope_depth);
            LLVMBuildBr(ctx->builder, end_bb);
        }
        else
        {
            ir_gen_emit_defers_at_depth(ctx, ctx->current_scope_depth);
        }
        ctx->current_scope_depth--;
        generator_pop_scope(ctx->gen_ctx);
        ctx->fallthrough_target_bb = NULL;

        // Position builder at the miss block for next iteration
        if (i < case_count - 1)
        {
            LLVMPositionBuilderAtEnd(ctx->builder, miss_bb);
        }
    }

    // Default case — always position at default_bb
    if (default_bb)
    {
        LLVMPositionBuilderAtEnd(ctx->builder, default_bb);
        ctx->current_scope_depth++;
        generator_push_scope(ctx->gen_ctx);
        for (size_t ci = 0; ci < default_case->list.count; ci++)
        {
            odin_grammar_node_t * stmt = default_case->list.children[ci];
            if (stmt == NULL)
                continue;
            ir_gen_node(ctx, stmt);
        }
        LLVMBasicBlockRef def_block_end = LLVMGetInsertBlock(ctx->builder);
        LLVMValueRef def_term = LLVMGetLastInstruction(def_block_end);
        if (def_term == NULL || !LLVMIsATerminatorInst(def_term))
        {
            ir_gen_emit_defers_at_depth(ctx, ctx->current_scope_depth);
            LLVMBuildBr(ctx->builder, end_bb);
        }
        else
        {
            ir_gen_emit_defers_at_depth(ctx, ctx->current_scope_depth);
        }
        ctx->current_scope_depth--;
        generator_pop_scope(ctx->gen_ctx);
    }

    // Pop loop context
    if (ctx->loop_depth > 0)
        ctx->loop_depth--;

    LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
    return NULL;
}

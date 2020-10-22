struct OutputStruct {
    out0: Integer
    out1: Integer
}
struct AggPayload {
    gb_term_attr0 : Integer
    agg_term_attr0: IntegerSumAggregate
}
struct AggValues {
    gb_term_attr0 : Integer
    agg_term_attr0: Integer
}
struct QueryState {
    execCtx     : *ExecutionContext
    aggHashTable: AggregationHashTable
}
struct P2_State {
    filterManager: FilterManager
    aggHashTable1: AggregationHashTable
    execFeatures : ExecOUFeatureVector
}
struct P1_State {
    output_buffer: *OutputBuffer
    execFeatures1: ExecOUFeatureVector
}
fun Query0_Pipeline1_KeyCheckPartial(lhs: *AggPayload, rhs: *AggPayload) -> bool {
    if (SqlBoolToBool(lhs.gb_term_attr0 != rhs.gb_term_attr0)) {
        return false
    }
    return true
}

fun Query0_Pipeline1_MergePartitions(queryState: *QueryState, aggHashTable: *AggregationHashTable, ahtOvfIter: *AHTOverflowPartitionIterator) -> nil {
    for (; @aggPartIterHasNext(ahtOvfIter); @aggPartIterNext(ahtOvfIter)) {
        var hashVal = @aggPartIterGetHash(ahtOvfIter)
        var partialRow = @ptrCast(*AggPayload, @aggPartIterGetRow(ahtOvfIter))
        var aggPayload = @ptrCast(*AggPayload, @aggHTLookup(aggHashTable, hashVal, Query0_Pipeline1_KeyCheckPartial, partialRow))
        if (aggPayload == nil) {
            @aggHTLink(aggHashTable, @aggPartIterGetRowEntry(ahtOvfIter))
        } else {
            @aggMerge(&aggPayload.agg_term_attr0, &partialRow.agg_term_attr0)
        }
    }
    return
}

fun Query0_Pipeline1_KeyCheck(aggPayload: *AggPayload, aggValues: *AggValues) -> bool {
    if (SqlBoolToBool(aggPayload.gb_term_attr0 != aggValues.gb_term_attr0)) {
        return false
    }
    return true
}

fun Query0_Pipeline2_FilterClause(execCtx: *ExecutionContext, vp: *VectorProjection, tids: *TupleIdList, context: *uint8) -> nil {
    @filterLt(execCtx, vp, 0, @intToSql(1000), tids)
    return
}

fun Query0_Init(queryState: *QueryState) -> nil {
    @aggHTInit(&queryState.aggHashTable, queryState.execCtx, @sizeOf(AggPayload))
    return
}

fun Query0_Pipeline2_PreHook(queryState: *QueryState, pipelineState: *P2_State, dummyArg: *nil) -> nil {
    @execOUFeatureVectorInit(queryState.execCtx, &pipelineState.execFeatures, 2, true)
    @registerThreadWithMetricsManager(queryState.execCtx)
    @execCtxStartPipelineTracker(queryState.execCtx, 2)
    return
}

fun Query0_Pipeline2_PostHook(queryState: *QueryState, pipelineState: *P2_State, overrideValue: uint32) -> nil {
    var num_tuples = @aggHTGetInsertCount(&queryState.aggHashTable)
    @execCtxEndPipelineTracker(queryState.execCtx, 0, 2, &pipelineState.execFeatures)
    @aggregateMetricsThread(queryState.execCtx)
    @execOUFeatureVectorReset(&pipelineState.execFeatures)
    return
}

fun Query0_Pipeline2_InitPipelineState(queryState: *QueryState, pipelineState: *P2_State) -> nil {
    @aggHTInit(&pipelineState.aggHashTable1, queryState.execCtx, @sizeOf(AggPayload))
    @filterManagerInit(&pipelineState.filterManager, queryState.execCtx)
    @filterManagerInsertFilter(&pipelineState.filterManager, Query0_Pipeline2_FilterClause)
    return
}

fun Query0_Pipeline2_TearDownPipelineState(queryState: *QueryState, pipelineState: *P2_State) -> nil {
    @aggHTFree(&pipelineState.aggHashTable1)
    @filterManagerFree(&pipelineState.filterManager)
    @execOUFeatureVectorReset(&pipelineState.execFeatures)
    return
}

fun Query0_Pipeline2_ParallelWork(queryState: *QueryState, pipelineState: *P2_State, tvi: *TableVectorIterator) -> nil {
    @execOUFeatureVectorInit(queryState.execCtx, &pipelineState.execFeatures, 2, false)
    @registerThreadWithMetricsManager(queryState.execCtx)
    @execCtxStartPipelineTracker(queryState.execCtx, 2)
    var slot: TupleSlot
    for (@tableIterAdvance(tvi)) {
        var vpi = @tableIterGetVPI(tvi)
        @filterManagerRunFilters(&pipelineState.filterManager, vpi, queryState.execCtx)
        for (; @vpiHasNextFiltered(vpi); @vpiAdvanceFiltered(vpi)) {
            slot = @vpiGetSlot(vpi)
            var aggValues: AggValues
            aggValues.gb_term_attr0 = @vpiGetInt(vpi, 1)
            aggValues.agg_term_attr0 = @vpiGetInt(vpi, 0)
            var hashVal = @hash(aggValues.gb_term_attr0)
            var aggPayload = @ptrCast(*AggPayload, @aggHTLookup(&pipelineState.aggHashTable1, hashVal, Query0_Pipeline1_KeyCheck, &aggValues))
            if (aggPayload == nil) {
                aggPayload = @ptrCast(*AggPayload, @aggHTInsert(&pipelineState.aggHashTable1, hashVal, true))
                aggPayload.gb_term_attr0 = aggValues.gb_term_attr0
                @aggInit(&aggPayload.agg_term_attr0)
            }
            @aggAdvance(&aggPayload.agg_term_attr0, &aggValues.agg_term_attr0)
        }
        var vpi_num_tuples = @tableIterGetVPINumTuples(tvi)
    }
    @execCtxEndPipelineTracker(queryState.execCtx, 0, 2, &pipelineState.execFeatures)
    @aggregateMetricsThread(queryState.execCtx)
    @execOUFeatureVectorReset(&pipelineState.execFeatures)
    return
}

fun Query0_Pipeline2_Init(queryState: *QueryState) -> nil {
    var threadStateContainer = @execCtxGetTLS(queryState.execCtx)
    @tlsReset(threadStateContainer, @sizeOf(P2_State), Query0_Pipeline2_InitPipelineState, Query0_Pipeline2_TearDownPipelineState, queryState)
    return
}

fun Query0_Pipeline2_Run(queryState: *QueryState) -> nil {
    var pipelineState = @ptrCast(*P2_State, @tlsGetCurrentThreadState(@execCtxGetTLS(queryState.execCtx)))
    var col_oids: [2]uint32
    col_oids[0] = 1
    col_oids[1] = 2
    @iterateTableParallel(1002, col_oids, queryState, queryState.execCtx, Query0_Pipeline2_ParallelWork)
    @execCtxInitHooks(queryState.execCtx, 2)
    @execCtxRegisterHook(queryState.execCtx, 0, Query0_Pipeline2_PreHook)
    @execCtxRegisterHook(queryState.execCtx, 1, Query0_Pipeline2_PostHook)
    @aggHTMoveParts(&queryState.aggHashTable, @execCtxGetTLS(queryState.execCtx), @offsetOf(P2_State, aggHashTable1), Query0_Pipeline1_MergePartitions)
    @execCtxClearHooks(queryState.execCtx)
    return
}

fun Query0_Pipeline2_TearDown(queryState: *QueryState) -> nil {
    @tlsClear(@execCtxGetTLS(queryState.execCtx))
    @checkTrackersStopped(queryState.execCtx)
    return
}

fun Query0_Pipeline1_InitPipelineState(queryState: *QueryState, pipelineState: *P1_State) -> nil {
    pipelineState.output_buffer = @resultBufferNew(queryState.execCtx)
    return
}

fun Query0_Pipeline1_TearDownPipelineState(queryState: *QueryState, pipelineState: *P1_State) -> nil {
    @resultBufferFree(pipelineState.output_buffer)
    @execOUFeatureVectorReset(&pipelineState.execFeatures1)
    return
}

fun Query0_Pipeline1_ParallelWork(queryState: *QueryState, pipelineState: *P1_State, aggHashTable: *AggregationHashTable) -> nil {
    @execOUFeatureVectorInit(queryState.execCtx, &pipelineState.execFeatures1, 1, false)
    @registerThreadWithMetricsManager(queryState.execCtx)
    @execCtxStartPipelineTracker(queryState.execCtx, 1)
    var iterBase: AHTIterator
    var iter = &iterBase
    for (@aggHTIterInit(iter, aggHashTable); @aggHTIterHasNext(iter); @aggHTIterNext(iter)) {
        var aggRow = @ptrCast(*AggPayload, @aggHTIterGetRow(iter))
        var outRow = @ptrCast(*OutputStruct, @resultBufferAllocRow(pipelineState.output_buffer))
        outRow.out0 = aggRow.gb_term_attr0
        outRow.out1 = @aggResult(&aggRow.agg_term_attr0)
    }
    @aggHTIterClose(iter)
    @resultBufferFinalize(pipelineState.output_buffer)
    @execCtxEndPipelineTracker(queryState.execCtx, 0, 1, &pipelineState.execFeatures1)
    @aggregateMetricsThread(queryState.execCtx)
    @execOUFeatureVectorReset(&pipelineState.execFeatures1)
    return
}

fun Query0_Pipeline1_Init(queryState: *QueryState) -> nil {
    var threadStateContainer = @execCtxGetTLS(queryState.execCtx)
    @tlsReset(threadStateContainer, @sizeOf(P1_State), Query0_Pipeline1_InitPipelineState, Query0_Pipeline1_TearDownPipelineState, queryState)
    return
}

fun Query0_Pipeline1_Run(queryState: *QueryState) -> nil {
    var pipelineState = @ptrCast(*P1_State, @tlsGetCurrentThreadState(@execCtxGetTLS(queryState.execCtx)))
    @aggHTParallelPartScan(&queryState.aggHashTable, queryState, @execCtxGetTLS(queryState.execCtx), Query0_Pipeline1_ParallelWork)
    @resultBufferFinalize(pipelineState.output_buffer)
    return
}

fun Query0_Pipeline1_TearDown(queryState: *QueryState) -> nil {
    @tlsClear(@execCtxGetTLS(queryState.execCtx))
    @checkTrackersStopped(queryState.execCtx)
    return
}

fun Query0_TearDown(queryState: *QueryState) -> nil {
    @aggHTFree(&queryState.aggHashTable)
    return
}
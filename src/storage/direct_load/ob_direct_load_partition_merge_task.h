// Copyright (c) 2022-present Oceanbase Inc. All Rights Reserved.
// Author:
//   suzhi.yt <>

#pragma once

#include "lib/list/ob_dlist.h"
#include "share/ob_tablet_autoincrement_param.h"
#include "storage/direct_load/ob_direct_load_data_fuse.h"
#include "storage/direct_load/ob_direct_load_external_scanner.h"
#include "storage/direct_load/ob_direct_load_merge_ctx.h"
#include "storage/direct_load/ob_direct_load_multiple_heap_table_scanner.h"
#include "sql/engine/expr/ob_expr_sys_op_opnsize.h"

namespace oceanbase
{
namespace blocksstable
{
class ObDatumRange;
} // namespace blocksstable
namespace common
{
class ObOptOSGColumnStat;
} // namespace common
namespace storage
{
class ObDirectLoadOriginTable;
class ObDirectLoadExternalTable;
class ObDirectLoadMultipleSSTable;
class ObDirectLoadMultipleHeapTable;

class ObDirectLoadPartitionMergeTask : public common::ObDLinkBase<ObDirectLoadPartitionMergeTask>
{
public:
  ObDirectLoadPartitionMergeTask();
  virtual ~ObDirectLoadPartitionMergeTask();
  int process();
  const common::ObIArray<ObOptOSGColumnStat*> &get_column_stat_array() const
  {
    return column_stat_array_;
  }
  int64_t get_row_count() const { return affected_rows_; }
  void stop();
  TO_STRING_KV(KPC_(merge_param), KPC_(merge_ctx), K_(parallel_idx));
protected:
  virtual int construct_row_iter(common::ObIAllocator &allocator,
                                 ObIStoreRowIterator *&row_iter) = 0;
private:
  int init_sql_statistics();
  int collect_obj(const blocksstable::ObDatumRow &datum_row);
protected:
  const ObDirectLoadMergeParam *merge_param_;
  ObDirectLoadTabletMergeCtx *merge_ctx_;
  int64_t parallel_idx_;
  int64_t affected_rows_;
  common::ObArray<ObOptOSGColumnStat*> column_stat_array_;
  common::ObArenaAllocator allocator_;
  volatile bool is_stop_;
  bool is_inited_;
};

class ObDirectLoadPartitionRangeMergeTask : public ObDirectLoadPartitionMergeTask
{
public:
  ObDirectLoadPartitionRangeMergeTask();
  virtual ~ObDirectLoadPartitionRangeMergeTask();
  int init(const ObDirectLoadMergeParam &merge_param,
           ObDirectLoadTabletMergeCtx *merge_ctx,
           ObDirectLoadOriginTable *origin_table,
           const common::ObIArray<ObDirectLoadSSTable *> &sstable_array,
           const blocksstable::ObDatumRange &range,
           int64_t parallel_idx);
protected:
  int construct_row_iter(common::ObIAllocator &allocator, ObIStoreRowIterator *&row_iter) override;
private:
  class RowIterator : public ObIStoreRowIterator
  {
  public:
    RowIterator();
    virtual ~RowIterator();
    int init(const ObDirectLoadMergeParam &merge_param,
             const common::ObTabletID &tablet_id,
             ObDirectLoadOriginTable *origin_table,
             const common::ObIArray<ObDirectLoadSSTable *> &sstable_array,
             const blocksstable::ObDatumRange &range);
    int get_next_row(const blocksstable::ObDatumRow *&datum_row) override;
  private:
    ObDirectLoadSSTableDataFuse data_fuse_;
    blocksstable::ObDatumRow datum_row_;
    int64_t rowkey_column_num_;
    bool is_inited_;
  };
private:
  ObDirectLoadOriginTable *origin_table_;
  const ObIArray<ObDirectLoadSSTable *> *sstable_array_;
  const blocksstable::ObDatumRange *range_;
};

class ObDirectLoadPartitionRangeMultipleMergeTask : public ObDirectLoadPartitionMergeTask
{
public:
  ObDirectLoadPartitionRangeMultipleMergeTask();
  virtual ~ObDirectLoadPartitionRangeMultipleMergeTask();
  int init(const ObDirectLoadMergeParam &merge_param,
           ObDirectLoadTabletMergeCtx *merge_ctx,
           ObDirectLoadOriginTable *origin_table,
           const common::ObIArray<ObDirectLoadMultipleSSTable *> &sstable_array,
           const blocksstable::ObDatumRange &range,
           int64_t parallel_idx);
protected:
  int construct_row_iter(common::ObIAllocator &allocator, ObIStoreRowIterator *&row_iter) override;
private:
  class RowIterator : public ObIStoreRowIterator
  {
  public:
    RowIterator();
    virtual ~RowIterator();
    int init(const ObDirectLoadMergeParam &merge_param,
             const common::ObTabletID &tablet_id,
             ObDirectLoadOriginTable *origin_table,
             const common::ObIArray<ObDirectLoadMultipleSSTable *> &sstable_array,
             const blocksstable::ObDatumRange &range);
    int get_next_row(const blocksstable::ObDatumRow *&datum_row) override;
  private:
    ObDirectLoadMultipleSSTableDataFuse data_fuse_;
    blocksstable::ObDatumRow datum_row_;
    int64_t rowkey_column_num_;
    bool is_inited_;
  };
private:
  ObDirectLoadOriginTable *origin_table_;
  const ObIArray<ObDirectLoadMultipleSSTable *> *sstable_array_;
  const blocksstable::ObDatumRange *range_;
};

class ObDirectLoadPartitionHeapTableMergeTask : public ObDirectLoadPartitionMergeTask
{
public:
  ObDirectLoadPartitionHeapTableMergeTask();
  virtual ~ObDirectLoadPartitionHeapTableMergeTask();
  int init(const ObDirectLoadMergeParam &merge_param,
           ObDirectLoadTabletMergeCtx *merge_ctx,
           ObDirectLoadExternalTable *external_table,
           const share::ObTabletCacheInterval &pk_interval,
           int64_t parallel_idx);
protected:
  int construct_row_iter(common::ObIAllocator &allocator, ObIStoreRowIterator *&row_iter) override;
private:
  class RowIterator : public ObIStoreRowIterator
  {
  public:
    RowIterator();
    virtual ~RowIterator();
    int init(const ObDirectLoadMergeParam &merge_param,
             const common::ObTabletID &tablet_id,
             ObDirectLoadExternalTable *external_table,
             const share::ObTabletCacheInterval &pk_interval);
    int get_next_row(const blocksstable::ObDatumRow *&datum_row) override;
  private:
    ObDirectLoadExternalSequentialScanner<ObDirectLoadExternalRow> scanner_;
    blocksstable::ObDatumRow datum_row_;
    blocksstable::ObStorageDatum *deserialize_datums_;
    int64_t deserialize_datum_cnt_;
    share::ObTabletCacheInterval pk_interval_;
    table::ObTableLoadResultInfo *result_info_;
    bool is_inited_;
  };
private:
  ObDirectLoadExternalTable *external_table_;
  share::ObTabletCacheInterval pk_interval_;
};

class ObDirectLoadPartitionHeapTableMultipleMergeTask : public ObDirectLoadPartitionMergeTask
{
public:
  ObDirectLoadPartitionHeapTableMultipleMergeTask();
  virtual ~ObDirectLoadPartitionHeapTableMultipleMergeTask();
  int init(const ObDirectLoadMergeParam &merge_param,
           ObDirectLoadTabletMergeCtx *merge_ctx,
           ObDirectLoadMultipleHeapTable *heap_table,
           const share::ObTabletCacheInterval &pk_interval,
           int64_t parallel_idx);
protected:
  int construct_row_iter(common::ObIAllocator &allocator, ObIStoreRowIterator *&row_iter) override;
private:
  class RowIterator : public ObIStoreRowIterator
  {
  public:
    RowIterator();
    virtual ~RowIterator();
    int init(const ObDirectLoadMergeParam &merge_param,
             const common::ObTabletID &tablet_id,
             ObDirectLoadMultipleHeapTable *heap_table,
             const share::ObTabletCacheInterval &pk_interval);
    int get_next_row(const blocksstable::ObDatumRow *&datum_row) override;
  private:
    ObDirectLoadMultipleHeapTableTabletWholeScanner scanner_;
    blocksstable::ObDatumRow datum_row_;
    blocksstable::ObStorageDatum *deserialize_datums_;
    int64_t deserialize_datum_cnt_;
    share::ObTabletCacheInterval pk_interval_;
    table::ObTableLoadResultInfo *result_info_;
    bool is_inited_;
  };
private:
  ObDirectLoadMultipleHeapTable *heap_table_;
  share::ObTabletCacheInterval pk_interval_;
};

class ObDirectLoadPartitionHeapTableMultipleAggregateMergeTask
  : public ObDirectLoadPartitionMergeTask
{
public:
  ObDirectLoadPartitionHeapTableMultipleAggregateMergeTask();
  virtual ~ObDirectLoadPartitionHeapTableMultipleAggregateMergeTask();
  int init(const ObDirectLoadMergeParam &merge_param, ObDirectLoadTabletMergeCtx *merge_ctx,
           ObDirectLoadOriginTable *origin_table,
           const common::ObIArray<ObDirectLoadMultipleHeapTable *> &heap_table_array,
           const share::ObTabletCacheInterval &pk_interval);
protected:
  int construct_row_iter(common::ObIAllocator &allocator, ObIStoreRowIterator *&row_iter) override;
private:
  class RowIterator : public ObIStoreRowIterator
  {
  public:
    RowIterator();
    virtual ~RowIterator();
    int init(const ObDirectLoadMergeParam &merge_param, const common::ObTabletID &tablet_id,
             ObDirectLoadOriginTable *origin_table,
             const common::ObIArray<ObDirectLoadMultipleHeapTable *> *heap_table_array,
             const share::ObTabletCacheInterval &pk_interval);
    int get_next_row(const blocksstable::ObDatumRow *&datum_row) override;
  private:
    int switch_next_heap_table();
  private:
    // for iter origin table
    common::ObArenaAllocator allocator_;
    blocksstable::ObDatumRange range_;
    ObIStoreRowIterator *origin_iter_;
    int64_t rowkey_column_num_;
    int64_t store_column_count_;
    // for iter multiple heap table
    common::ObTabletID tablet_id_;
    ObDirectLoadTableDataDesc table_data_desc_;
    const common::ObIArray<ObDirectLoadMultipleHeapTable *> *heap_table_array_;
    int64_t pos_;
    ObDirectLoadMultipleHeapTableTabletWholeScanner scanner_;
    blocksstable::ObDatumRow datum_row_;
    blocksstable::ObStorageDatum *deserialize_datums_;
    int64_t deserialize_datum_cnt_;
    share::ObTabletCacheInterval pk_interval_;
    table::ObTableLoadResultInfo *result_info_;
    bool is_inited_;
  };
private:
  ObDirectLoadOriginTable *origin_table_;
  const common::ObIArray<ObDirectLoadMultipleHeapTable *> *heap_table_array_;
  share::ObTabletCacheInterval pk_interval_;
};

} // namespace storage
} // namespace oceanbase

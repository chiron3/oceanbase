// Copyright (c) 2022-present Oceanbase Inc. All Rights Reserved.
// Author:
//   suzhi.yt <>

#pragma once

#include "lib/container/ob_loser_tree.h"
#include "lib/container/ob_se_array.h"
#include "storage/access/ob_simple_rows_merger.h"
#include "storage/access/ob_store_row_iterator.h"
#include "storage/direct_load/ob_direct_load_multiple_sstable.h"
#include "storage/direct_load/ob_direct_load_multiple_sstable_scan_merge_loser_tree.h"
#include "storage/direct_load/ob_direct_load_table_data_desc.h"
#include "share/table/ob_table_load_define.h"

namespace oceanbase
{
namespace blocksstable
{
class ObStorageDatumUtils;
} // namespace blocksstable
namespace observer
{
class ObTableLoadErrorRowHandler;
} // namespace observer
namespace storage
{

struct ObDirectLoadMultipleSSTableScanMergeParam
{
public:
  ObDirectLoadMultipleSSTableScanMergeParam();
  ~ObDirectLoadMultipleSSTableScanMergeParam();
  bool is_valid() const;
  TO_STRING_KV(K_(table_data_desc), KP_(datum_utils), KP_(error_row_handler), KP_(result_info));
public:
  ObDirectLoadTableDataDesc table_data_desc_;
  const blocksstable::ObStorageDatumUtils *datum_utils_;
  observer::ObTableLoadErrorRowHandler *error_row_handler_;
  table::ObTableLoadResultInfo *result_info_;
};

class ObDirectLoadMultipleSSTableScanMerge : public ObIStoreRowIterator
{
public:
  static const int64_t MAX_SSTABLE_COUNT = 1024;
  typedef ObDirectLoadMultipleSSTableScanMergeLoserTreeItem LoserTreeItem;
  typedef ObDirectLoadMultipleSSTableScanMergeLoserTreeCompare LoserTreeCompare;
  typedef ObSimpleRowsMerger<LoserTreeItem, LoserTreeCompare> ScanSimpleMerger;
  typedef common::ObLoserTree<LoserTreeItem, LoserTreeCompare, MAX_SSTABLE_COUNT>
    ScanMergeLoserTree;
public:
  ObDirectLoadMultipleSSTableScanMerge();
  ~ObDirectLoadMultipleSSTableScanMerge();
  void reset();
  int init(const ObDirectLoadMultipleSSTableScanMergeParam &param,
           const common::ObIArray<ObDirectLoadMultipleSSTable *> &sstable_array,
           const ObDirectLoadMultipleDatumRange &range);
  int get_next_row(const ObDirectLoadMultipleDatumRow *&external_row);
  int get_next_row(const blocksstable::ObDatumRow *&datum_row) override;
private:
  int init_rows_merger(int64_t sstable_count);
  int supply_consume();
  int inner_get_next_row(const ObDirectLoadMultipleDatumRow *&external_row);
private:
  common::ObArenaAllocator allocator_;
  ObDirectLoadTableDataDesc table_data_desc_;
  const blocksstable::ObStorageDatumUtils *datum_utils_;
  observer::ObTableLoadErrorRowHandler *error_row_handler_;
  table::ObTableLoadResultInfo *result_info_;
  const ObDirectLoadMultipleDatumRange *range_;
  common::ObSEArray<ObDirectLoadMultipleSSTableScanner *, 64> scanners_;
  int64_t *consumers_;
  int64_t consumer_cnt_;
  LoserTreeCompare compare_;
  ScanSimpleMerger *simple_merge_;
  ScanMergeLoserTree *loser_tree_;
  common::ObRowsMerger<LoserTreeItem, LoserTreeCompare> *rows_merger_;
  blocksstable::ObDatumRow datum_row_;
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase

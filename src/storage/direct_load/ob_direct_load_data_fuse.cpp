// Copyright (c) 2022-present Oceanbase Inc. All Rights Reserved.
// Author:
//   yiren.ly <>

#define USING_LOG_PREFIX STORAGE

#include "storage/direct_load/ob_direct_load_data_fuse.h"

namespace oceanbase
{
namespace storage
{
using namespace common;
using namespace blocksstable;
using namespace observer;
using namespace share;
using namespace sql;

/**
 * ObDirectLoadSSTableScanMergeParam
 */

ObDirectLoadDataFuseParam::ObDirectLoadDataFuseParam()
  : store_column_count_(0),
    datum_utils_(nullptr),
    error_row_handler_(nullptr),
    result_info_(nullptr)
{
}

ObDirectLoadDataFuseParam::~ObDirectLoadDataFuseParam()
{
}

bool ObDirectLoadDataFuseParam::is_valid() const
{
  return tablet_id_.is_valid() && store_column_count_ > 0 && table_data_desc_.is_valid() &&
         nullptr != datum_utils_ && nullptr != error_row_handler_ && nullptr != result_info_;
}

/**
 * TwoRowsMerger
 */
ObDirectLoadDataFuse::TwoRowsMerger::TwoRowsMerger()
  : rowkey_column_num_(0), item_cnt_(0), is_unique_champion_(false), is_inited_(false)
{
}

ObDirectLoadDataFuse::TwoRowsMerger::~TwoRowsMerger()
{
}

int ObDirectLoadDataFuse::TwoRowsMerger::init(int64_t rowkey_column_num,
                                              const ObStorageDatumUtils *datum_utils)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("TwoRowsMerger init twice", KR(ret), KP(this));
  } else {
    rowkey_column_num_ = rowkey_column_num;
    datum_utils_ = datum_utils;
    is_inited_ = true;
  }
  return ret;
}

int ObDirectLoadDataFuse::TwoRowsMerger::compare(const ObDatumRow &first_row,
                                                 const ObDatumRow &second_row, int &cmp_ret)
{
  int ret = OB_SUCCESS;
  ObDatumRowkey first_key(first_row.storage_datums_, rowkey_column_num_);
  ObDatumRowkey second_key(second_row.storage_datums_, rowkey_column_num_);
  if (OB_FAIL(first_key.compare(second_key, *datum_utils_, cmp_ret))) {
    LOG_WARN("fail to compare", KR(ret));
  }
  return ret;
}

int ObDirectLoadDataFuse::TwoRowsMerger::push(const Item &item)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("TwoRowsMerger not init", KR(ret));
  } else if (item_cnt_ >= ITER_COUNT) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to push", KR(ret));
  } else {
    items_[item_cnt_++] = item;
  }
  return ret;
}

int ObDirectLoadDataFuse::TwoRowsMerger::top(const Item *&item)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("TwoRowsMerger not init", KR(ret));
  } else if (item_cnt_ <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to top", KR(ret));
  } else {
    item = &(items_[item_cnt_ - 1]);
  }
  return ret;
}

int ObDirectLoadDataFuse::TwoRowsMerger::pop()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("TwoRowsMerger not init", KR(ret));
  } else if (item_cnt_ <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to top", KR(ret));
  } else {
    --item_cnt_;
  }
  return ret;
}

int ObDirectLoadDataFuse::TwoRowsMerger::rebuild()
{
  int ret = OB_SUCCESS;
  is_unique_champion_ = true;
  if (item_cnt_ == 2) {
    int cmp_ret = 0;
    if (OB_FAIL(compare(*items_[0].datum_row_, *items_[1].datum_row_, cmp_ret))) {
      LOG_WARN("compare failed", KR(ret));
    } else if (cmp_ret == 0) {
      is_unique_champion_ = false;
    } else if (cmp_ret < 0) {
      std::swap(items_[0], items_[1]);
    }
  }
  return ret;
}
/**
 * ObDirectLoadDataFuse
 */

ObDirectLoadDataFuse::ObDirectLoadDataFuse()
  : consumer_cnt_(0), is_inited_(false)
{
}

ObDirectLoadDataFuse::~ObDirectLoadDataFuse()
{
}

int ObDirectLoadDataFuse::init(const ObDirectLoadDataFuseParam &param,
                               ObIStoreRowIterator *origin_iter, ObIStoreRowIterator *load_iter)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObDirectLoadDataFuse init twice", KR(ret), KP(this));
  } else if (OB_UNLIKELY(!param.is_valid() || nullptr == origin_iter || nullptr == load_iter)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument", K(ret), K(param), KP(origin_iter), KP(load_iter));
  } else {
    param_ = param;
    if (OB_FAIL(rows_merger_.init(param.table_data_desc_.rowkey_column_num_, param.datum_utils_))) {
      LOG_WARN("fail to init rows merger", KR(ret));
    } else {
      iters_[ORIGIN_IDX] = origin_iter;
      iters_[LOAD_IDX] = load_iter;
      consumers_[consumer_cnt_++] = ORIGIN_IDX;
      consumers_[consumer_cnt_++] = LOAD_IDX;
      is_inited_ = true;
    }
  }
  return ret;
}

int ObDirectLoadDataFuse::supply_consume()
{
  int ret = OB_SUCCESS;
  Item item;
  for (int64_t i = 0; OB_SUCC(ret) && i < consumer_cnt_; ++i) {
    const int64_t iter_idx = consumers_[i];
    ObIStoreRowIterator *iter = iters_[iter_idx];
    if (OB_FAIL(iter->get_next_row(item.datum_row_))) {
      if (OB_UNLIKELY(OB_ITER_END != ret)) {
        LOG_WARN("fail to get next row from scanner", KR(ret));
      } else {
        ret = OB_SUCCESS;
      }
    } else if (OB_UNLIKELY(item.datum_row_->count_ != param_.store_column_count_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected column count", KR(ret), K(item.datum_row_->count_), K(param_.store_column_count_));
    } else {
      item.iter_idx_ = iter_idx;
      if (OB_FAIL(rows_merger_.push(item))) {
        LOG_WARN("fail to push item", KR(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    consumer_cnt_ = 0;
    if (OB_FAIL(rows_merger_.rebuild())) {
      LOG_WARN("fail to rebuild", KR(ret));
    }
  }
  return ret;
}

int ObDirectLoadDataFuse::inner_get_next_row(const ObDatumRow *&datum_row)
{
  int ret = OB_SUCCESS;
  if (rows_merger_.empty()) {
    ret = OB_ITER_END;
  } else if (rows_merger_.is_unique_champion()) {
    const Item *item = nullptr;
    if (OB_FAIL(rows_merger_.top(item))) {
      LOG_WARN("fail to rebuild", KR(ret));
    } else {
      datum_row = item->datum_row_;
      consumers_[consumer_cnt_++] = item->iter_idx_;
      if (OB_FAIL(rows_merger_.pop())) {
        LOG_WARN("fail to pop item", KR(ret));
      } else if (item->iter_idx_ == LOAD_IDX) {
        ATOMIC_INC(&param_.result_info_->rows_affected_);
      }
    }
  } else {
    const Item *item = nullptr;
    while (OB_SUCC(ret) && !rows_merger_.empty()) {
      if (OB_FAIL(rows_merger_.top(item))) {
        LOG_WARN("fail to rebuild", KR(ret));
      } else {
        if (ObLoadDupActionType::LOAD_STOP_ON_DUP == param_.dup_action_) {
          if (item->iter_idx_ == ORIGIN_IDX) {
            datum_row = item->datum_row_;
          } else {
            ObTableLoadErrorRowHandler *error_row_handler = param_.error_row_handler_;
            if (OB_FAIL(error_row_handler->append_error_row(*item->datum_row_))) {
              if ((OB_ERR_TOO_MANY_ROWS == ret)
                  && (0 == param_.error_row_handler_->get_capacity())){
                ret = OB_ERR_PRIMARY_KEY_DUPLICATE;
              }
              LOG_WARN("fail to append row to error row handler", KR(ret), KPC(item->datum_row_));
            }
          }
        } else if (ObLoadDupActionType::LOAD_IGNORE == param_.dup_action_) {
          if (item->iter_idx_ == ORIGIN_IDX) {
            datum_row = item->datum_row_;
          } else {
            ATOMIC_INC(&param_.result_info_->skipped_);
          }
        } else if (ObLoadDupActionType::LOAD_REPLACE == param_.dup_action_) {
          if (item->iter_idx_ == ORIGIN_IDX) {
            ATOMIC_INC(&param_.result_info_->deleted_);
            ATOMIC_AAF(&param_.result_info_->rows_affected_, 2);
          } else {
            datum_row = item->datum_row_;
          }
        }
        if (OB_SUCC(ret)) {
          consumers_[consumer_cnt_++] = item->iter_idx_;
          if (OB_FAIL(rows_merger_.pop())) {
            LOG_WARN("fail to pop item", KR(ret));
          }
        }
      }
    }
  }
  return ret;
}

int ObDirectLoadDataFuse::get_next_row(const ObDatumRow *&datum_row)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDirectLoadDataFuse not init", KR(ret), KP(this));
  } else {
    if (consumer_cnt_ > 0 && OB_FAIL(supply_consume())) {
      LOG_WARN("fail to supply consume", KR(ret));
    } else if (OB_FAIL(inner_get_next_row(datum_row))) {
      if (OB_UNLIKELY(OB_ITER_END != ret)) {
        LOG_WARN("fail to do inner get next row", KR(ret));
      }
    }
  }
  return ret;
}

/**
 * ObDirectLoadSSTableDataFuse
 */

ObDirectLoadSSTableDataFuse::ObDirectLoadSSTableDataFuse()
  : allocator_("TLD_DataFuse"), origin_iter_(nullptr), is_inited_(false)
{
}

ObDirectLoadSSTableDataFuse::~ObDirectLoadSSTableDataFuse()
{
  if (nullptr != origin_iter_) {
    origin_iter_->~ObIStoreRowIterator();
    allocator_.free(origin_iter_);
    origin_iter_ = nullptr;
  }
}

int ObDirectLoadSSTableDataFuse::init(const ObDirectLoadDataFuseParam &param,
                                      ObDirectLoadOriginTable *origin_table,
                                      const ObIArray<ObDirectLoadSSTable *> &sstable_array,
                                      const ObDatumRange &range)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObDirectLoadSSTableDataFuse init twice", KR(ret), KP(this));
  } else if (OB_UNLIKELY(!param.is_valid()) || OB_ISNULL(origin_table) || !range.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument", K(ret), K(param), KP(origin_table), K(range));
  } else {
    allocator_.set_tenant_id(MTL_ID());
    // construct iters
    if (OB_FAIL(origin_table->scan(range, allocator_, origin_iter_))) {
      LOG_WARN("fail to scan origin table", KR(ret));
    } else {
      ObDirectLoadSSTableScanMergeParam scan_merge_param;
      scan_merge_param.tablet_id_ = param.tablet_id_;
      scan_merge_param.table_data_desc_ = param.table_data_desc_;
      scan_merge_param.datum_utils_ = param.datum_utils_;
      scan_merge_param.error_row_handler_ = param.error_row_handler_;
      scan_merge_param.result_info_ = param.result_info_;
      if (OB_FAIL(scan_merge_.init(scan_merge_param, sstable_array, range))) {
        LOG_WARN("fail to init scan merge", KR(ret));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(data_fuse_.init(param, origin_iter_, &scan_merge_))) {
        LOG_WARN("fail to init data fuse", KR(ret));
      } else {
        is_inited_ = true;
      }
    }
  }
  return ret;
}

int ObDirectLoadSSTableDataFuse::get_next_row(const ObDatumRow *&datum_row)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDirectLoadSSTableDataFuse not init", KR(ret), KP(this));
  } else {
    ret = data_fuse_.get_next_row(datum_row);
  }
  return ret;
}

/**
 * ObDirectLoadMultipleSSTableDataFuse
 */

ObDirectLoadMultipleSSTableDataFuse::ObDirectLoadMultipleSSTableDataFuse()
  : allocator_("TLD_DataFuse"), origin_iter_(nullptr), is_inited_(false)
{
}

ObDirectLoadMultipleSSTableDataFuse::~ObDirectLoadMultipleSSTableDataFuse()
{
  if (nullptr != origin_iter_) {
    origin_iter_->~ObIStoreRowIterator();
    allocator_.free(origin_iter_);
    origin_iter_ = nullptr;
  }
}

int ObDirectLoadMultipleSSTableDataFuse::init(
  const ObDirectLoadDataFuseParam &param,
  ObDirectLoadOriginTable *origin_table,
  const ObIArray<ObDirectLoadMultipleSSTable *> &sstable_array,
  const ObDatumRange &range)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObDirectLoadMultipleSSTableDataFuse init twice", KR(ret), KP(this));
  } else if (OB_UNLIKELY(!param.is_valid()) || OB_ISNULL(origin_table) || !range.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument", K(ret), K(param), KP(origin_table), K(range));
  } else {
    allocator_.set_tenant_id(MTL_ID());
    if (OB_FAIL(range_.assign(param.tablet_id_, range))) {
      LOG_WARN("fail to assign range", KR(ret));
    }
    // construct iters
    else if (OB_FAIL(origin_table->scan(range, allocator_, origin_iter_))) {
      LOG_WARN("fail to scan origin table", KR(ret));
    } else {
      ObDirectLoadMultipleSSTableScanMergeParam scan_merge_param;
      scan_merge_param.table_data_desc_ = param.table_data_desc_;
      scan_merge_param.datum_utils_ = param.datum_utils_;
      scan_merge_param.error_row_handler_ = param.error_row_handler_;
      scan_merge_param.result_info_ = param.result_info_;
      if (OB_FAIL(scan_merge_.init(scan_merge_param, sstable_array, range_))) {
        LOG_WARN("fail to init scan merge", KR(ret));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(data_fuse_.init(param, origin_iter_, &scan_merge_))) {
        LOG_WARN("fail to init data fuse", KR(ret));
      } else {
        is_inited_ = true;
      }
    }
  }
  return ret;
}

int ObDirectLoadMultipleSSTableDataFuse::get_next_row(const ObDatumRow *&datum_row)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDirectLoadSSTableDataFuse not init", KR(ret), KP(this));
  } else {
    ret = data_fuse_.get_next_row(datum_row);
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase

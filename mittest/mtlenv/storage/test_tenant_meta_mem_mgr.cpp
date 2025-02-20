/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#include <gtest/gtest.h>

#define USING_LOG_PREFIX STORAGE

#define protected public
#define private public

#include "storage/meta_mem/ob_tenant_meta_mem_mgr.h"
#include "storage/ls/ob_ls.h"
#include "storage/mock_ob_log_handler.h"
#include "storage/tablelock/ob_lock_memtable.h"
#include "storage/tablet/ob_tablet_table_store_flag.h"
#include "storage/tablet/ob_tablet_create_delete_helper.h"
#include "storage/tablet/ob_tablet_slog_helper.h"
#include "storage/tablet/ob_tablet_status.h"
#include "mtlenv/mock_tenant_module_env.h"
#include "storage/test_dml_common.h"
#include "observer/ob_safe_destroy_thread.h"

namespace oceanbase
{
using namespace share;
namespace storage
{
int ObTenantCheckpointSlogHandler::read_from_ckpt(const ObMetaDiskAddr &phy_addr,
    char *buf, const int64_t buf_len, int64_t &r_len)
{
  return OB_NOT_SUPPORTED;
}

int ObTablet::update_msd_cache_on_pointer()
{
  return OB_SUCCESS;
}

class TestTenantMetaMemMgr : public ::testing::Test
{
public:
  TestTenantMetaMemMgr();
  virtual ~TestTenantMetaMemMgr() = default;

  virtual void SetUp() override;
  virtual void TearDown() override;
  static void SetUpTestCase();
  static void TearDownTestCase();

  void prepare_data_schema(ObTableSchema &table_schema);

public:
  static const int64_t TEST_ROWKEY_COLUMN_CNT = 3;
  static const int64_t TEST_COLUMN_CNT = 6;
  static const uint64_t TEST_TENANT_ID = 1;
  static const int64_t TEST_LS_ID = 101;

public:
  const uint64_t tenant_id_;
  share::ObLSID ls_id_;
  ObTenantMetaMemMgr t3m_;
};

// record the old_t3m just for ls remove
ObTenantMetaMemMgr *old_t3m = nullptr;
TestTenantMetaMemMgr::TestTenantMetaMemMgr()
  : tenant_id_(TEST_TENANT_ID),
    ls_id_(TEST_LS_ID),
    t3m_(TEST_TENANT_ID)
{
}

void TestTenantMetaMemMgr::SetUpTestCase()
{
  int ret = OB_SUCCESS;
  ret = MockTenantModuleEnv::get_instance().init();
  ASSERT_EQ(OB_SUCCESS, ret);
  SAFE_DESTROY_INSTANCE.init();
  SAFE_DESTROY_INSTANCE.start();
  ObServerCheckpointSlogHandler::get_instance().is_started_ = true;

  // create ls
  ObLSHandle ls_handle;
  ret = TestDmlCommon::create_ls(TEST_TENANT_ID, ObLSID(TEST_LS_ID), ls_handle);
  old_t3m = MTL(ObTenantMetaMemMgr *);
  ASSERT_EQ(OB_SUCCESS, ret);
}

void TestTenantMetaMemMgr::SetUp()
{
  int ret = OB_SUCCESS;
  ret = t3m_.init();
  ASSERT_EQ(OB_SUCCESS, ret);

  ObTenantBase *tenant_base = MTL_CTX();
  tenant_base->set(&t3m_);
}

void TestTenantMetaMemMgr::TearDownTestCase()
{
  int ret = OB_SUCCESS;
  ret = MTL(ObLSService*)->remove_ls(ObLSID(TEST_LS_ID), false);
  ASSERT_EQ(OB_SUCCESS, ret);

  SAFE_DESTROY_INSTANCE.stop();
  SAFE_DESTROY_INSTANCE.wait();
  SAFE_DESTROY_INSTANCE.destroy();
  MockTenantModuleEnv::get_instance().destroy();
}

void TestTenantMetaMemMgr::TearDown()
{
  t3m_.destroy();

  // return to the old t3m to make ls destroy success.
  ObTenantBase *tenant_base = MTL_CTX();
  tenant_base->set(old_t3m);
}

void TestTenantMetaMemMgr::prepare_data_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  const uint64_t table_id = 219039915101;
  int64_t micro_block_size = 16 * 1024;
  ObColumnSchemaV2 column;

  table_schema.reset();
  ret = table_schema.set_table_name("test_ls_tablet_service_data_table");
  ASSERT_EQ(OB_SUCCESS, ret);
  table_schema.set_tenant_id(TEST_TENANT_ID);
  table_schema.set_tablegroup_id(1);
  table_schema.set_database_id(1);
  table_schema.set_table_id(table_id);
  table_schema.set_rowkey_column_num(TEST_ROWKEY_COLUMN_CNT);
  table_schema.set_max_used_column_id(TEST_COLUMN_CNT);
  table_schema.set_block_size(micro_block_size);
  table_schema.set_compress_func_name("none");
  table_schema.set_row_store_type(FLAT_ROW_STORE);
  //init column
  char name[OB_MAX_FILE_NAME_LENGTH];
  memset(name, 0, sizeof(name));
  const int64_t column_ids[] = {16,17,20,21,22,23,24,29};
  for(int64_t i = 0; i < TEST_COLUMN_CNT; ++i){
    ObObjType obj_type = ObIntType;
    const int64_t column_id = column_ids[i];

    if (i == 1) {
      obj_type = ObVarcharType;
    }
    column.reset();
    column.set_table_id(table_id);
    column.set_column_id(column_id);
    sprintf(name, "test%020ld", i);
    ASSERT_EQ(OB_SUCCESS, column.set_column_name(name));
    column.set_data_type(obj_type);
    column.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
    column.set_data_length(10);
    if (i < TEST_ROWKEY_COLUMN_CNT) {
      column.set_rowkey_position(i + 1);
    } else {
      column.set_rowkey_position(0);
    }
    LOG_INFO("add column", K(i), K(column));
    ASSERT_EQ(OB_SUCCESS, table_schema.add_column(column));
  }
  LOG_INFO("dump data table schema", LITERAL_K(TEST_ROWKEY_COLUMN_CNT), K(table_schema));
}

class TestConcurrentT3M : public share::ObThreadPool
{
public:
  TestConcurrentT3M(
      const int64_t thread_cnt,
      ObTenantMetaMemMgr &t3m_,
      ObTenantBase *tenant_base);
  virtual ~TestConcurrentT3M();
  virtual void run1();

  static const int64_t TABLET_CNT_PER_THREAD = 1000;

private:
  int64_t thread_cnt_;
  share::ObLSID ls_id_;
  ObTenantMetaMemMgr &t3m_;
  ObTenantBase *tenant_base_;
  common::SpinRWLock lock_;
};

TestConcurrentT3M::TestConcurrentT3M(
    const int64_t thread_cnt,
    ObTenantMetaMemMgr &t3m,
    ObTenantBase *tenant_base)
  : thread_cnt_(thread_cnt),
    ls_id_(TestTenantMetaMemMgr::TEST_LS_ID),
    t3m_(t3m),
    tenant_base_(tenant_base),
    lock_()
{
  set_thread_count(static_cast<int32_t>(thread_cnt_));
}

TestConcurrentT3M::~TestConcurrentT3M()
{
}

void TestConcurrentT3M::run1()
{
  int ret = OB_SUCCESS;
  const int N = 131;
  ObLSHandle ls_handle;
  ObTabletHandle handle;
  ObTabletMapKey key;
  key.ls_id_ = ls_id_;

  share::ObTenantEnv::set_tenant(tenant_base_);

  ObLSService *ls_svr = MTL(ObLSService*);
  ret = ls_svr->get_ls(ls_id_, ls_handle, ObLSGetMod::STORAGE_MOD);
  ASSERT_EQ(OB_SUCCESS, ret);

  int count = TABLET_CNT_PER_THREAD;
  while (count-- > 0) {
    ObTablet *tablet = nullptr;
    key.tablet_id_ = thread_idx_ * TABLET_CNT_PER_THREAD * 10 + count + 1;
    int ret = t3m_.acquire_tablet(WashTabletPriority::WTP_HIGH, key, ls_handle, handle, false);
    ASSERT_EQ(common::OB_SUCCESS, ret);
    ASSERT_TRUE(handle.is_valid());

    tablet = handle.get_obj();
    ASSERT_TRUE(nullptr != tablet);
    ASSERT_TRUE(tablet->pointer_hdl_.is_valid());

    ObTabletHandle tmp_handle;
    ret = t3m_.get_tablet(WashTabletPriority::WTP_HIGH, key, tmp_handle);
    ASSERT_EQ(common::OB_ITEM_NOT_SETTED, ret);
    ASSERT_TRUE(!tmp_handle.is_valid());

    tablet = handle.get_obj();
    ASSERT_TRUE(nullptr != tablet);
    ASSERT_TRUE(nullptr != handle.obj_pool_);

    ObMetaDiskAddr addr;
    ret = t3m_.compare_and_swap_tablet(key, addr, handle, handle);
    ASSERT_EQ(common::OB_INVALID_ARGUMENT, ret);

    addr.first_id_ = 1;
    addr.second_id_ = 2;
    addr.offset_ = 0;
    addr.size_ = 4096;
    addr.type_ = ObMetaDiskAddr::DiskType::BLOCK;

    ret = t3m_.compare_and_swap_tablet(key, addr, handle, handle);
    ASSERT_EQ(common::OB_SUCCESS, ret);

    ObMetaPointerHandle<ObTabletMapKey, ObTablet> ptr_hdl(t3m_.tablet_map_);
    ret = t3m_.tablet_map_.map_.get_refactored(key, ptr_hdl.ptr_);
    ASSERT_EQ(common::OB_SUCCESS, ret);
    ret = t3m_.tablet_map_.inc_handle_ref(ptr_hdl.ptr_);
    ASSERT_EQ(common::OB_SUCCESS, ret);
    ASSERT_TRUE(addr == ptr_hdl.ptr_->ptr_->phy_addr_);
    ASSERT_TRUE(handle.obj_ == ptr_hdl.ptr_->ptr_->obj_.ptr_);
    ASSERT_TRUE(handle.obj_pool_ == ptr_hdl.ptr_->ptr_->obj_.pool_);

    handle.reset();

    if (0 == count % N) {
      ret = t3m_.del_tablet(key);
      ASSERT_EQ(common::OB_SUCCESS, ret);
    }
  }
}

TEST_F(TestTenantMetaMemMgr, test_bucket_cnt)
{
  const int64_t unify_bucket_num = common::hash::cal_next_prime(t3m_.cal_adaptive_bucket_num());
  const int64_t t3m_lock_bkt_cnt = t3m_.bucket_lock_.bucket_cnt_;
  const int64_t t3m_map_bkt_cnt = t3m_.tablet_map_.map_.bucket_count();
  const int64_t t3m_map_lock_bkt_cnt = t3m_.tablet_map_.bucket_lock_.bucket_cnt_;
  ASSERT_NE(unify_bucket_num, 0);
  // ASSERT_EQ(unify_bucket_num, t3m_lock_bkt_cnt);
  ASSERT_EQ(unify_bucket_num, t3m_map_bkt_cnt);
  ASSERT_EQ(unify_bucket_num, t3m_map_lock_bkt_cnt);

  const int64_t unify_pin_set_bkt_cnt = common::hash::cal_next_prime(ObTenantMetaMemMgr::DEFAULT_BUCKET_NUM);
  const int64_t pin_set_lock_bkt_cnt = t3m_.pin_set_lock_.bucket_cnt_;
  const int64_t pin_set_bkt_cnt = t3m_.pinned_tablet_set_.ht_.get_bucket_count();
  ASSERT_NE(unify_pin_set_bkt_cnt, 0);
  ASSERT_EQ(unify_pin_set_bkt_cnt, pin_set_lock_bkt_cnt);
  ASSERT_EQ(unify_pin_set_bkt_cnt, pin_set_bkt_cnt);
}

TEST_F(TestTenantMetaMemMgr, test_sstable)
{
  int ret = OB_SUCCESS;
  ObTableHandleV2 handle;

  ret = t3m_.acquire_sstable(handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_TRUE(nullptr != handle.table_);
  ASSERT_EQ(1, t3m_.sstable_pool_.inner_used_num_);
  ASSERT_EQ(1, handle.get_table()->get_ref());

  handle.table_->inc_ref();
  t3m_.release_sstable(static_cast<ObSSTable *>(handle.table_));
  ASSERT_EQ(1, t3m_.sstable_pool_.inner_used_num_);
  ASSERT_EQ(2, handle.get_table()->get_ref());

  handle.table_->dec_ref();
  t3m_.release_sstable(static_cast<ObSSTable *>(handle.table_));
  ASSERT_EQ(1, t3m_.sstable_pool_.inner_used_num_);
  ASSERT_EQ(1, handle.get_table()->get_ref());

  handle.reset();
  ASSERT_EQ(1, t3m_.sstable_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.free_tables_queue_.size());

  t3m_.release_sstable(static_cast<ObSSTable *>(handle.table_));
}

TEST_F(TestTenantMetaMemMgr, test_memtable)
{
  int ret = OB_SUCCESS;
  ObTableHandleV2 handle;

  ret = t3m_.acquire_memtable(handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_TRUE(nullptr != handle.table_);
  ASSERT_EQ(1, t3m_.memtable_pool_.inner_used_num_);
  ASSERT_EQ(1, handle.get_table()->get_ref());

  handle.table_->inc_ref();
  t3m_.release_memtable(static_cast<memtable::ObMemtable *>(handle.table_));
  ASSERT_EQ(1, t3m_.memtable_pool_.inner_used_num_);
  ASSERT_EQ(2, handle.get_table()->get_ref());

  handle.table_->dec_ref();
  t3m_.release_memtable(static_cast<memtable::ObMemtable *>(handle.table_));
  ASSERT_EQ(1, t3m_.memtable_pool_.inner_used_num_);
  ASSERT_EQ(1, handle.get_table()->get_ref());

  handle.reset();
  ASSERT_EQ(1, t3m_.memtable_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.free_tables_queue_.size());

  t3m_.release_memtable(static_cast<memtable::ObMemtable *>(handle.table_));
}

TEST_F(TestTenantMetaMemMgr, test_tx_ctx_memtable)
{
  int ret = OB_SUCCESS;
  ObTableHandleV2 handle;

  ret = t3m_.acquire_tx_ctx_memtable(handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_TRUE(nullptr != handle.table_);
  ASSERT_EQ(1, t3m_.tx_ctx_memtable_pool_.inner_used_num_);
  ASSERT_EQ(1, handle.get_table()->get_ref());

  handle.table_->inc_ref();
  t3m_.release_tx_ctx_memtable_(static_cast<ObTxCtxMemtable *>(handle.table_));
  ASSERT_EQ(1, t3m_.tx_ctx_memtable_pool_.inner_used_num_);
  ASSERT_EQ(2, handle.get_table()->get_ref());

  handle.table_->dec_ref();
  t3m_.release_tx_ctx_memtable_(static_cast<ObTxCtxMemtable *>(handle.table_));
  ASSERT_EQ(1, t3m_.tx_ctx_memtable_pool_.inner_used_num_);
  ASSERT_EQ(1, handle.get_table()->get_ref());

  handle.reset();
  ASSERT_EQ(1, t3m_.tx_ctx_memtable_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.free_tables_queue_.size());

  t3m_.release_tx_ctx_memtable_(static_cast<ObTxCtxMemtable *>(handle.table_));
}

TEST_F(TestTenantMetaMemMgr, test_tx_data_memtable)
{
  int ret = OB_SUCCESS;
  ObTableHandleV2 handle;

  ret = t3m_.acquire_tx_data_memtable(handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_TRUE(nullptr != handle.table_);
  ASSERT_EQ(1, t3m_.tx_data_memtable_pool_.inner_used_num_);
  ASSERT_EQ(1, handle.get_table()->get_ref());

  handle.table_->inc_ref();
  t3m_.release_tx_data_memtable_(static_cast<ObTxDataMemtable *>(handle.table_));
  ASSERT_EQ(1, t3m_.tx_data_memtable_pool_.inner_used_num_);
  ASSERT_EQ(2, handle.get_table()->get_ref());

  handle.table_->dec_ref();
  t3m_.release_tx_data_memtable_(static_cast<ObTxDataMemtable *>(handle.table_));
  ASSERT_EQ(1, t3m_.tx_data_memtable_pool_.inner_used_num_);
  ASSERT_EQ(1, handle.get_table()->get_ref());

  handle.reset();
  ASSERT_EQ(1, t3m_.tx_data_memtable_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.free_tables_queue_.size());

  t3m_.release_tx_data_memtable_(static_cast<ObTxDataMemtable *>(handle.table_));
}

TEST_F(TestTenantMetaMemMgr, test_lock_memtable)
{
  int ret = OB_SUCCESS;
  ObTableHandleV2 handle;

  ret = t3m_.acquire_lock_memtable(handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_TRUE(nullptr != handle.table_);
  ASSERT_EQ(1, t3m_.lock_memtable_pool_.inner_used_num_);
  ASSERT_EQ(1, handle.get_table()->get_ref());

  handle.table_->inc_ref();
  t3m_.release_lock_memtable_(static_cast<transaction::tablelock::ObLockMemtable *>(handle.table_));
  ASSERT_EQ(1, t3m_.lock_memtable_pool_.inner_used_num_);
  ASSERT_EQ(2, handle.get_table()->get_ref());

  handle.table_->dec_ref();
  t3m_.release_lock_memtable_(static_cast<transaction::tablelock::ObLockMemtable *>(handle.table_));
  ASSERT_EQ(1, t3m_.lock_memtable_pool_.inner_used_num_);
  ASSERT_EQ(1, handle.get_table()->get_ref());

  handle.reset();
  ASSERT_EQ(1, t3m_.lock_memtable_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.free_tables_queue_.size());

  t3m_.release_lock_memtable_(static_cast<transaction::tablelock::ObLockMemtable *>(handle.table_));
}

TEST_F(TestTenantMetaMemMgr, test_tablet)
{
  int ret = OB_SUCCESS;
  const ObTabletID tablet_id(10000001);
  const ObTabletMapKey key(ls_id_, tablet_id);
  ObTablet *tablet = nullptr;
  ObLSHandle ls_handle;
  ObTabletHandle handle;

  ObLSService *ls_svr = MTL(ObLSService*);
  ret = ls_svr->get_ls(ls_id_, ls_handle, ObLSGetMod::STORAGE_MOD);
  ASSERT_EQ(OB_SUCCESS, ret);

  ret = t3m_.get_tablet(WashTabletPriority::WTP_HIGH, key, handle);
  ASSERT_EQ(common::OB_ENTRY_NOT_EXIST, ret);
  ASSERT_TRUE(!handle.is_valid());

  tablet = handle.get_obj();
  ASSERT_TRUE(nullptr == tablet);

  ret = t3m_.acquire_tablet(WashTabletPriority::WTP_HIGH, key, ls_handle, handle, false);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_TRUE(handle.is_valid());

  tablet = handle.get_obj();
  ASSERT_TRUE(nullptr != tablet);
  ASSERT_TRUE(tablet->pointer_hdl_.is_valid());

  ASSERT_TRUE(handle.calc_wash_score(handle.wash_priority_) > 0);

  ObTabletHandle tmp_handle;
  ret = t3m_.get_tablet(WashTabletPriority::WTP_HIGH, key, tmp_handle);
  ASSERT_EQ(common::OB_ITEM_NOT_SETTED, ret);
  ASSERT_TRUE(!tmp_handle.is_valid());

  ASSERT_TRUE(nullptr != tablet);
  ASSERT_TRUE(nullptr != handle.obj_pool_);
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());

  ObMetaDiskAddr addr;
  ret = t3m_.compare_and_swap_tablet(key, addr, handle, handle);
  ASSERT_EQ(common::OB_INVALID_ARGUMENT, ret);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());

  addr.first_id_ = 1;
  addr.second_id_ = 2;
  addr.offset_ = 0;
  addr.size_ = 4096;
  addr.type_ = ObMetaDiskAddr::DiskType::BLOCK;

  ret = t3m_.compare_and_swap_tablet(key, addr, handle, handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());

  ObMetaPointerHandle<ObTabletMapKey, ObTablet> ptr_hdl(t3m_.tablet_map_);
  ret = t3m_.tablet_map_.map_.get_refactored(key, ptr_hdl.ptr_);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ret = t3m_.tablet_map_.inc_handle_ref(ptr_hdl.ptr_);
  ASSERT_TRUE(addr == ptr_hdl.ptr_->ptr_->phy_addr_);
  ASSERT_TRUE(handle.obj_ == ptr_hdl.ptr_->ptr_->obj_.ptr_);
  ASSERT_TRUE(handle.obj_pool_ == ptr_hdl.ptr_->ptr_->obj_.pool_);

  ObMetaDiskAddr old_addr;
  old_addr.set_none_addr();
  ret = t3m_.compare_and_swap_tablet_pure_address_without_object(key, old_addr, addr);
  ASSERT_EQ(common::OB_INVALID_ARGUMENT, ret);
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_TRUE(handle.is_valid());

  handle.reset();
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());

  ret = t3m_.get_tablet(WashTabletPriority::WTP_HIGH, key, handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_TRUE(handle.is_valid());

  tablet = handle.get_obj();
  ASSERT_TRUE(nullptr != tablet);
  ASSERT_TRUE(tablet->pointer_hdl_.is_valid());

  tablet->tablet_meta_.ls_id_ = key.ls_id_;
  tablet->tablet_meta_.tablet_id_ = key.tablet_id_;
  handle.reset();

  ret = t3m_.try_wash_tablet(1);
  ASSERT_EQ(common::OB_NOT_INIT, ret);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);

  ret = t3m_.del_tablet(key);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(0, t3m_.tablet_map_.map_.size());

  ptr_hdl.reset();
  ASSERT_EQ(0, t3m_.tablet_pool_.inner_used_num_);
}

TEST_F(TestTenantMetaMemMgr, test_wash_tablet)
{
  int ret = OB_SUCCESS;
  const ObTabletID tablet_id(1000000001);
  const ObTabletMapKey key(ls_id_, tablet_id);
  ObTablet *tablet = nullptr;
  ObLSHandle ls_handle;
  ObTabletHandle handle;

  ObLSService *ls_svr = MTL(ObLSService*);
  ret = ls_svr->get_ls(ls_id_, ls_handle, ObLSGetMod::STORAGE_MOD);
  ASSERT_EQ(OB_SUCCESS, ret);

  ret = t3m_.acquire_tablet(WashTabletPriority::WTP_HIGH, key, ls_handle, handle, false);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_TRUE(handle.is_valid());

  tablet = handle.get_obj();
  ASSERT_TRUE(nullptr != tablet);
  ASSERT_TRUE(tablet->pointer_hdl_.is_valid());

  // set tx data
  ObTabletTxMultiSourceDataUnit &tx_data = tablet->tablet_meta_.tx_data_;
  tx_data.tx_id_ = ObTabletCommon::FINAL_TX_ID;
  share::SCN scn;
  scn.convert_for_logservice(12345);
  tx_data.tx_scn_ = scn;
  tx_data.tablet_status_ = ObTabletStatus::NORMAL;

  ObTableHandleV2 table_handle;
  checkpoint::ObCheckpointExecutor ckpt_executor;
  checkpoint::ObDataCheckpoint data_checkpoint;
  ObLS ls;
  ObLSTxService ls_tx_service(&ls);
  ObLSWRSHandler ls_loop_worker;
  ObLSTabletService ls_tablet_svr;
  MockObLogHandler log_handler;
  ObFreezer freezer;
  ObTableSchema table_schema;
  prepare_data_schema(table_schema);

  ret = freezer.init(&ls);
  ASSERT_EQ(common::OB_SUCCESS, ret);

  ret = t3m_.acquire_sstable(table_handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  table_handle.get_table()->set_table_type(ObITable::TableType::MAJOR_SSTABLE);

  share::SCN create_scn;
  create_scn.convert_from_ts(ObTimeUtility::fast_current_time());

  ObTabletID empty_tablet_id;
  ObTabletTableStoreFlag store_flag;
  store_flag.set_with_major_sstable();
  ret = tablet->init(ls_id_, tablet_id, tablet_id, empty_tablet_id, empty_tablet_id,
      create_scn, create_scn.get_val_for_tx(), table_schema,
      lib::Worker::CompatMode::MYSQL, store_flag, table_handle, &freezer);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, tablet->get_ref());

  ObMetaDiskAddr addr;
  addr.first_id_ = 1;
  addr.second_id_ = 2;
  addr.offset_ = 0;
  addr.size_ = 4096;
  addr.type_ = ObMetaDiskAddr::DiskType::BLOCK;

  ret = t3m_.compare_and_swap_tablet(key, addr, handle, handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());

  handle.reset();
  ASSERT_EQ(1, tablet->get_ref());
  ret = t3m_.try_wash_tablet(1);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(0, t3m_.tablet_pool_.inner_used_num_);

  ObMetaDiskAddr none_addr;
  none_addr.set_none_addr();
  ret = t3m_.compare_and_swap_tablet_pure_address_without_object(key, addr, none_addr);
  ASSERT_EQ(common::OB_INVALID_ARGUMENT, ret);

  ret = t3m_.tablet_map_.erase(key);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(0, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(0, t3m_.tablet_pool_.inner_used_num_);
}

TEST_F(TestTenantMetaMemMgr, test_wash_inner_tablet)
{
  int ret = OB_SUCCESS;
  const ObTabletID tablet_id(300);
  const ObTabletMapKey key(ls_id_, tablet_id);
  ObTablet *tablet = nullptr;
  ObLSHandle ls_handle;
  ObTabletHandle handle;

  ObLSService *ls_svr = MTL(ObLSService*);
  ret = ls_svr->get_ls(ls_id_, ls_handle, ObLSGetMod::STORAGE_MOD);
  ASSERT_EQ(OB_SUCCESS, ret);

  ret = t3m_.acquire_tablet(WashTabletPriority::WTP_HIGH, key, ls_handle, handle, false);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_TRUE(handle.is_valid());

  tablet = handle.get_obj();
  ASSERT_TRUE(nullptr != tablet);
  ASSERT_TRUE(tablet->pointer_hdl_.is_valid());

  ObTableHandleV2 table_handle;
  checkpoint::ObCheckpointExecutor ckpt_executor;
  checkpoint::ObDataCheckpoint data_checkpoint;
  ObLS ls;
  ObLSTxService ls_tx_service(&ls);
  ObLSWRSHandler ls_loop_worker;
  ObLSTabletService ls_tablet_svr;
  MockObLogHandler log_handler;
  ObFreezer freezer;
  ObTableSchema table_schema;
  prepare_data_schema(table_schema);

  ret = freezer.init(&ls);
  ASSERT_EQ(common::OB_SUCCESS, ret);

  ret = t3m_.acquire_sstable(table_handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  table_handle.get_table()->set_table_type(ObITable::TableType::MAJOR_SSTABLE);

  share::SCN create_scn;
  create_scn.convert_from_ts(ObTimeUtility::fast_current_time());

  ObTabletID empty_tablet_id;
  ObTabletTableStoreFlag store_flag;
  store_flag.set_with_major_sstable();
  ret = tablet->init(ls_id_, tablet_id, tablet_id, empty_tablet_id, empty_tablet_id,
      create_scn, create_scn.get_val_for_tx(), table_schema, lib::Worker::CompatMode::MYSQL,
      store_flag, table_handle, &freezer);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, tablet->get_ref());

  // set tx data
  ObTabletTxMultiSourceDataUnit &tx_data = tablet->tablet_meta_.tx_data_;
  tx_data.tx_id_ = ObTabletCommon::FINAL_TX_ID;
  share::SCN scn;
  scn.convert_for_logservice(12345);
  tx_data.tx_scn_ = scn;
  tx_data.tablet_status_ = ObTabletStatus::NORMAL;

  ObMetaDiskAddr addr;
  addr.first_id_ = 1;
  addr.second_id_ = 2;
  addr.offset_ = 0;
  addr.size_ = 4096;
  addr.type_ = ObMetaDiskAddr::DiskType::BLOCK;

  ret = t3m_.compare_and_swap_tablet(key, addr, handle, handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());

  handle.reset();
  ASSERT_EQ(1, tablet->get_ref());
  ret = t3m_.try_wash_tablet(1);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(0, t3m_.tablet_pool_.inner_used_num_);

  ObMetaDiskAddr mem_addr;
  ret = mem_addr.set_mem_addr(0, 0);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ret = t3m_.compare_and_swap_tablet_pure_address_without_object(key, addr, mem_addr);
  ASSERT_EQ(common::OB_INVALID_ARGUMENT, ret);

  ret = t3m_.tablet_map_.erase(key);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(0, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(0, t3m_.tablet_pool_.inner_used_num_);
}


TEST_F(TestTenantMetaMemMgr, test_wash_no_sstable_tablet)
{
  int ret = OB_SUCCESS;
  const ObTabletID tablet_id(12345678);
  const ObTabletMapKey key(ls_id_, tablet_id);
  ObTablet *tablet = nullptr;
  ObLSHandle ls_handle;
  ObTabletHandle handle;

  ObLSService *ls_svr = MTL(ObLSService*);
  ret = ls_svr->get_ls(ls_id_, ls_handle, ObLSGetMod::STORAGE_MOD);
  ASSERT_EQ(OB_SUCCESS, ret);

  ret = t3m_.acquire_tablet(WashTabletPriority::WTP_HIGH, key, ls_handle, handle, false);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_TRUE(handle.is_valid());

  tablet = handle.get_obj();
  ASSERT_TRUE(nullptr != tablet);
  ASSERT_TRUE(tablet->pointer_hdl_.is_valid());

  // set tx data
  ObTabletTxMultiSourceDataUnit &tx_data = tablet->tablet_meta_.tx_data_;
  tx_data.tx_id_ = ObTabletCommon::FINAL_TX_ID;
  share::SCN scn;
  scn.convert_for_logservice(12345);
  tx_data.tx_scn_ = scn;
  tx_data.tablet_status_ = ObTabletStatus::NORMAL;

  ObTableHandleV2 table_handle;
  checkpoint::ObCheckpointExecutor ckpt_executor;
  checkpoint::ObDataCheckpoint data_checkpoint;
  ObLS ls;
  ObLSTxService ls_tx_service(&ls);
  ObLSWRSHandler ls_loop_worker;
  ObLSTabletService ls_tablet_svr;
  MockObLogHandler log_handler;
  ObFreezer freezer;
  ObTableSchema table_schema;
  prepare_data_schema(table_schema);

  ret = freezer.init(&ls);
  ASSERT_EQ(common::OB_SUCCESS, ret);

  share::SCN create_scn;
  create_scn.convert_from_ts(ObTimeUtility::fast_current_time());

  ObTabletID empty_tablet_id;
  ObTabletTableStoreFlag store_flag;
  store_flag.set_with_major_sstable();
  ret = tablet->init(ls_id_, tablet_id, tablet_id, empty_tablet_id, empty_tablet_id,
      create_scn, create_scn.get_val_for_tx(), table_schema, lib::Worker::CompatMode::MYSQL,
      store_flag, table_handle, &freezer);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, tablet->get_ref());

  ObMetaDiskAddr addr;
  addr.first_id_ = 1;
  addr.second_id_ = 2;
  addr.offset_ = 0;
  addr.size_ = 4096;
  addr.type_ = ObMetaDiskAddr::DiskType::BLOCK;

  ret = t3m_.compare_and_swap_tablet(key, addr, handle, handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());

  handle.reset();
  ASSERT_EQ(1, tablet->get_ref());
  ret = t3m_.try_wash_tablet(1);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(0, t3m_.tablet_pool_.inner_used_num_);

  ret = t3m_.tablet_map_.erase(key);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(0, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(0, t3m_.tablet_pool_.inner_used_num_);
}

TEST_F(TestTenantMetaMemMgr, test_not_wash_in_tx_tablet)
{
  int ret = OB_SUCCESS;
  const ObTabletID tablet_id(1234567890);
  const ObTabletMapKey key(ls_id_, tablet_id);
  ObTablet *tablet = nullptr;
  ObLSHandle ls_handle;
  ObTabletHandle handle;

  ObLSService *ls_svr = MTL(ObLSService*);
  ret = ls_svr->get_ls(ls_id_, ls_handle, ObLSGetMod::STORAGE_MOD);
  ASSERT_EQ(common::OB_SUCCESS, ret);

  ret = t3m_.acquire_tablet(WashTabletPriority::WTP_HIGH, key, ls_handle, handle, false);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_TRUE(handle.is_valid());

  tablet = handle.get_obj();
  ASSERT_TRUE(nullptr != tablet);
  ASSERT_TRUE(tablet->pointer_hdl_.is_valid());

  ObTableHandleV2 table_handle;
  checkpoint::ObCheckpointExecutor ckpt_executor;
  checkpoint::ObDataCheckpoint data_checkpoint;
  ObLS ls;
  ObLSTxService ls_tx_service(&ls);
  ObLSWRSHandler ls_loop_worker;
  ObLSTabletService ls_tablet_svr;
  MockObLogHandler log_handler;
  ObFreezer freezer;
  ObTableSchema table_schema;
  prepare_data_schema(table_schema);

  ret = freezer.init(&ls);
  ASSERT_EQ(common::OB_SUCCESS, ret);

  share::SCN create_scn;
  create_scn.convert_from_ts(ObTimeUtility::fast_current_time());

  ObTabletID empty_tablet_id;
  ObTabletTableStoreFlag store_flag;
  store_flag.set_with_major_sstable();
  ret = tablet->init(ls_id_, tablet_id, tablet_id, empty_tablet_id, empty_tablet_id,
      create_scn, create_scn.get_val_for_tx(), table_schema, lib::Worker::CompatMode::MYSQL,
      store_flag, table_handle, &freezer);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, tablet->get_ref());

  ObMetaDiskAddr addr;
  addr.first_id_ = 1;
  addr.second_id_ = 2;
  addr.offset_ = 0;
  addr.size_ = 4096;
  addr.type_ = ObMetaDiskAddr::DiskType::BLOCK;

  ret = t3m_.compare_and_swap_tablet(key, addr, handle, handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());

  // set tx data, status is DELETING
  ObTabletTxMultiSourceDataUnit &tx_data = tablet->tablet_meta_.tx_data_;
  tx_data.tx_id_ = 666;
  share::SCN scn;
  scn.convert_for_logservice(888);
  tx_data.tx_scn_ = scn;
  tx_data.tablet_status_ = ObTabletStatus::DELETING;
  ret = handle.get_obj()->set_tx_data_in_tablet_pointer(tx_data);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(common::OB_SUCCESS, t3m_.insert_pinned_tablet(key));
  handle.reset();
  ASSERT_EQ(1, tablet->get_ref());
  ret = t3m_.try_wash_tablet(1);
  ASSERT_EQ(common::OB_ALLOCATE_MEMORY_FAILED, ret); // wash failed
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);

  // set status to DELETED
  tx_data.tx_id_ = ObTabletCommon::FINAL_TX_ID;
  scn.convert_for_logservice(999);
  tx_data.tx_scn_ = scn;
  tx_data.tablet_status_ = ObTabletStatus::DELETED;
  ret = t3m_.get_tablet(WashTabletPriority::WTP_HIGH, key, handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ret = handle.get_obj()->set_tx_data_in_tablet_pointer(tx_data);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  handle.reset();
  ASSERT_EQ(common::OB_SUCCESS, t3m_.erase_pinned_tablet(key));

  ret = t3m_.try_wash_tablet(1);
  ASSERT_EQ(common::OB_SUCCESS, ret); // wash succeeded
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(0, t3m_.tablet_pool_.inner_used_num_);

  ret = t3m_.tablet_map_.erase(key);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(0, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(0, t3m_.tablet_pool_.inner_used_num_);
}

TEST_F(TestTenantMetaMemMgr, test_get_tablet_with_allocator)
{
  int ret = OB_SUCCESS;
  const ObTabletID tablet_id(1000000001);
  const ObTabletMapKey key(ls_id_, tablet_id);
  ObTablet *tablet = nullptr;
  ObLSHandle ls_handle;
  ObTabletHandle handle;

  ObLSService *ls_svr = MTL(ObLSService*);
  ret = ls_svr->get_ls(ls_id_, ls_handle, ObLSGetMod::STORAGE_MOD);
  ASSERT_EQ(OB_SUCCESS, ret);

  ret = t3m_.acquire_tablet(WashTabletPriority::WTP_HIGH, key, ls_handle, handle, false);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_TRUE(handle.is_valid());

  tablet = handle.get_obj();
  ASSERT_TRUE(nullptr != tablet);
  ASSERT_TRUE(tablet->pointer_hdl_.is_valid());

  ObTabletTxMultiSourceDataUnit &tx_data = tablet->tablet_meta_.tx_data_;
  tx_data.tx_id_ = ObTabletCommon::FINAL_TX_ID;
  share::SCN scn;
  scn.convert_for_logservice(12345);
  tx_data.tx_scn_ = scn;
  tx_data.tablet_status_ = ObTabletStatus::NORMAL;

  ObTableHandleV2 table_handle;
  checkpoint::ObCheckpointExecutor ckpt_executor;
  checkpoint::ObDataCheckpoint data_checkpoint;
  ObLS ls;
  ObLSTxService ls_tx_service(&ls);
  ObLSWRSHandler ls_loop_worker;
  ObLSTabletService ls_tablet_svr;
  MockObLogHandler log_handler;
  ObFreezer freezer;
  ObTableSchema table_schema;
  ObTabletCreateSSTableParam param;
  prepare_data_schema(table_schema);

  ret = freezer.init(&ls);
  ASSERT_EQ(common::OB_SUCCESS, ret);

  ret = t3m_.acquire_sstable(table_handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  const int64_t multi_version_col_cnt = ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
  param.table_key_.table_type_ = ObITable::TableType::MAJOR_SSTABLE;
  param.table_key_.tablet_id_ = tablet_id;
  param.table_key_.version_range_.base_version_ = ObVersionRange::MIN_VERSION;
  param.table_key_.version_range_.snapshot_version_ = 1;
  param.schema_version_ = table_schema.get_schema_version();
  param.create_snapshot_version_ = 0;
  param.progressive_merge_round_ = table_schema.get_progressive_merge_round();
  param.progressive_merge_step_ = 0;
  param.table_mode_ = table_schema.get_table_mode_struct();
  param.index_type_ = table_schema.get_index_type();
  param.rowkey_column_cnt_ = table_schema.get_rowkey_column_num()
            + ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
  param.root_block_addr_.set_none_addr();
  param.data_block_macro_meta_addr_.set_none_addr();
  param.root_row_store_type_ = ObRowStoreType::FLAT_ROW_STORE;
  param.latest_row_store_type_ = ObRowStoreType::FLAT_ROW_STORE;
  param.data_index_tree_height_ = 0;
  param.index_blocks_cnt_ = 0;
  param.data_blocks_cnt_ = 0;
  param.micro_block_cnt_ = 0;
  param.use_old_macro_block_count_ = 0;
  param.column_cnt_ = table_schema.get_column_count() + multi_version_col_cnt;
  param.data_checksum_ = 0;
  param.occupy_size_ = 0;
  param.ddl_scn_.set_min();
  param.filled_tx_scn_.set_min();
  param.original_size_ = 0;
  param.compressor_type_ = ObCompressorType::NONE_COMPRESSOR;
  param.encrypt_id_ = 0;
  param.master_key_id_ = 0;
  ASSERT_EQ(OB_SUCCESS, ObSSTableMergeRes::fill_column_checksum_for_empty_major(param.column_cnt_, param.column_checksums_));
  ASSERT_EQ(OB_SUCCESS, static_cast<ObSSTable *>(table_handle.get_table())->init(param, &t3m_.get_tenant_allocator()));

  share::SCN create_scn;
  create_scn.convert_from_ts(ObTimeUtility::fast_current_time());

  ObTabletID empty_tablet_id;
  ObTabletTableStoreFlag store_flag;
  store_flag.set_with_major_sstable();
  ret = tablet->init(ls_id_, tablet_id, tablet_id, empty_tablet_id, empty_tablet_id,
      create_scn, create_scn.get_val_for_tx(), table_schema,
      lib::Worker::CompatMode::MYSQL, store_flag, table_handle, &freezer);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, tablet->get_ref());

  ObMetaDiskAddr addr;
  ret = ObTabletSlogHelper::write_create_tablet_slog(handle, addr);
  ASSERT_EQ(common::OB_SUCCESS, ret);

  ret = t3m_.compare_and_swap_tablet(key, addr, handle, handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);

  handle.reset();
  ASSERT_EQ(1, tablet->get_ref());
  ret = t3m_.try_wash_tablet(1);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(0, t3m_.tablet_pool_.inner_used_num_);

  common::ObArenaAllocator allocator;
  ASSERT_EQ(common::OB_SUCCESS, t3m_.get_tablet_with_allocator(WashTabletPriority::WTP_HIGH, key, allocator, handle));
  ASSERT_TRUE(handle.is_valid());

  ret = t3m_.tablet_map_.erase(key);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(0, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(0, t3m_.tablet_pool_.inner_used_num_);
}

TEST_F(TestTenantMetaMemMgr, test_wash_mem_tablet)
{
  return;
  int ret = OB_SUCCESS;
  const ObTabletID tablet_id(1000000001);
  const ObTabletMapKey key(ls_id_, tablet_id);
  ObTablet *tablet = nullptr;
  ObLSHandle ls_handle;
  ObTabletHandle handle;

  ObLSService *ls_svr = MTL(ObLSService*);
  ret = ls_svr->get_ls(ls_id_, ls_handle, ObLSGetMod::STORAGE_MOD);
  ASSERT_EQ(OB_SUCCESS, ret);

  ret = t3m_.acquire_tablet(WashTabletPriority::WTP_HIGH, key, ls_handle, handle, false);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_TRUE(handle.is_valid());

  tablet = handle.get_obj();
  ASSERT_TRUE(nullptr != tablet);
  ASSERT_TRUE(tablet->pointer_hdl_.is_valid());

  // set tx data
  ObTabletTxMultiSourceDataUnit &tx_data = tablet->tablet_meta_.tx_data_;
  tx_data.tx_id_ = ObTabletCommon::FINAL_TX_ID;
  tx_data.tx_scn_.convert_for_logservice(12345);
  tx_data.tablet_status_ = ObTabletStatus::NORMAL;

  ObTableHandleV2 table_handle;
  checkpoint::ObCheckpointExecutor ckpt_executor;
  checkpoint::ObDataCheckpoint data_checkpoint;
  ObLS ls;
  ObLSTxService ls_tx_service(&ls);
  ObLSWRSHandler ls_loop_worker;
  ObLSTabletService ls_tablet_svr;
  MockObLogHandler log_handler;
  ObFreezer freezer;
  ObTableSchema table_schema;
  ObTabletCreateSSTableParam param;
  prepare_data_schema(table_schema);

  ret = freezer.init(&ls);
  ASSERT_EQ(common::OB_SUCCESS, ret);

  ret = t3m_.acquire_sstable(table_handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  const int64_t multi_version_col_cnt = ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
  param.table_key_.table_type_ = ObITable::TableType::MAJOR_SSTABLE;
  param.table_key_.tablet_id_ = tablet_id;
  param.table_key_.version_range_.base_version_ = ObVersionRange::MIN_VERSION;
  param.table_key_.version_range_.snapshot_version_ = 1;
  param.schema_version_ = table_schema.get_schema_version();
  param.create_snapshot_version_ = 0;
  param.progressive_merge_round_ = table_schema.get_progressive_merge_round();
  param.progressive_merge_step_ = 0;
  param.table_mode_ = table_schema.get_table_mode_struct();
  param.index_type_ = table_schema.get_index_type();
  param.rowkey_column_cnt_ = table_schema.get_rowkey_column_num()
            + ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
  param.root_block_addr_.set_none_addr();
  param.data_block_macro_meta_addr_.set_none_addr();
  param.root_row_store_type_ = ObRowStoreType::FLAT_ROW_STORE;
  param.latest_row_store_type_ = ObRowStoreType::FLAT_ROW_STORE;
  param.data_index_tree_height_ = 0;
  param.index_blocks_cnt_ = 0;
  param.data_blocks_cnt_ = 0;
  param.micro_block_cnt_ = 0;
  param.use_old_macro_block_count_ = 0;
  param.column_cnt_ = table_schema.get_column_count() + multi_version_col_cnt;
  param.data_checksum_ = 0;
  param.occupy_size_ = 0;
  param.ddl_scn_.set_min();
  param.filled_tx_scn_.set_min();
  param.original_size_ = 0;
  param.compressor_type_ = ObCompressorType::NONE_COMPRESSOR;
  param.encrypt_id_ = 0;
  param.master_key_id_ = 0;
  ASSERT_EQ(OB_SUCCESS, ObSSTableMergeRes::fill_column_checksum_for_empty_major(param.column_cnt_, param.column_checksums_));
  ASSERT_EQ(OB_SUCCESS, static_cast<ObSSTable *>(table_handle.get_table())->init(param, &t3m_.get_tenant_allocator()));

  share::SCN create_scn;
  create_scn.convert_from_ts(ObTimeUtility::fast_current_time());

  ObTabletID empty_tablet_id;
  ObTabletTableStoreFlag store_flag;
  store_flag.set_with_major_sstable();
  ret = tablet->init(ls_id_, tablet_id, tablet_id, empty_tablet_id, empty_tablet_id,
      create_scn, create_scn.get_val_for_tx(), table_schema,
      lib::Worker::CompatMode::MYSQL, store_flag, table_handle, &freezer);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, tablet->get_ref());

  ObMetaDiskAddr addr;
  addr.offset_ = 0;
  addr.size_ = 4096;
  addr.type_ = ObMetaDiskAddr::DiskType::MEM;

  ret = t3m_.compare_and_swap_tablet(key, addr, handle, handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());

  handle.reset();
  ASSERT_EQ(1, tablet->get_ref());
  ret = t3m_.try_wash_tablet(1);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(0, t3m_.tablet_pool_.inner_used_num_);

  ObMetaDiskAddr none_addr;
  none_addr.set_none_addr();
  ret = t3m_.compare_and_swap_tablet_pure_address_without_object(key, addr, none_addr);
  ASSERT_EQ(common::OB_INVALID_ARGUMENT, ret);

  ret = t3m_.tablet_map_.erase(key);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(0, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(0, t3m_.tablet_pool_.inner_used_num_);
}

TEST_F(TestTenantMetaMemMgr, test_replace_tablet)
{
  int ret = OB_SUCCESS;
  const ObTabletID tablet_id(101);
  const ObTabletMapKey key(ls_id_, tablet_id);
  ObTablet *tablet = nullptr;
  ObLSHandle ls_handle;
  ObTabletHandle handle;

  ObLSService *ls_svr = MTL(ObLSService*);
  ret = ls_svr->get_ls(ls_id_, ls_handle, ObLSGetMod::STORAGE_MOD);
  ASSERT_EQ(OB_SUCCESS, ret);

  ret = t3m_.acquire_tablet(WashTabletPriority::WTP_HIGH, key, ls_handle, handle, false);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  tablet = handle.get_obj();
  ASSERT_TRUE(nullptr != tablet);

  tablet->inc_ref();
  handle.reset();
  ASSERT_TRUE(!handle.is_valid());
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());

  ret = t3m_.del_tablet(key);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);
  ASSERT_EQ(0, t3m_.tablet_map_.map_.size());

  t3m_.release_tablet(tablet);
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);
  ASSERT_EQ(0, t3m_.tablet_map_.map_.size());

  tablet->dec_ref();
  t3m_.release_tablet(tablet);
  tablet = nullptr;
  ASSERT_EQ(0, t3m_.tablet_pool_.inner_used_num_);
  ASSERT_EQ(0, t3m_.tablet_map_.map_.size());

  t3m_.release_tablet(tablet);
  ASSERT_EQ(0, t3m_.tablet_pool_.inner_used_num_);
  ASSERT_EQ(0, t3m_.tablet_map_.map_.size());

  ret = t3m_.acquire_tablet(WashTabletPriority::WTP_HIGH, key, ls_handle, handle, false);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_TRUE(handle.is_valid());

  tablet = handle.get_obj();
  ASSERT_TRUE(nullptr != tablet);
  ASSERT_TRUE(tablet->pointer_hdl_.is_valid());

  ObTabletHandle tmp_handle;
  ret = t3m_.get_tablet(WashTabletPriority::WTP_HIGH, key, tmp_handle);
  ASSERT_EQ(common::OB_ITEM_NOT_SETTED, ret);
  ASSERT_TRUE(!tmp_handle.is_valid());

  ObMetaDiskAddr addr;
  ret = t3m_.compare_and_swap_tablet(key, addr, handle, handle);
  ASSERT_EQ(common::OB_INVALID_ARGUMENT, ret);
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_TRUE(handle.is_valid());

  addr.first_id_ = 1;
  addr.second_id_ = 2;
  addr.offset_ = 0;
  addr.size_ = 4096;
  addr.type_ = ObMetaDiskAddr::DiskType::BLOCK;

  ret = t3m_.compare_and_swap_tablet(key, addr, handle, handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);

  ObMetaPointerHandle<ObTabletMapKey, ObTablet> ptr_hdl(t3m_.tablet_map_);
  ret = t3m_.tablet_map_.map_.get_refactored(key, ptr_hdl.ptr_);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ret = t3m_.tablet_map_.inc_handle_ref(ptr_hdl.ptr_);
  ASSERT_TRUE(addr == ptr_hdl.ptr_->ptr_->phy_addr_);
  ASSERT_TRUE(handle.obj_ == ptr_hdl.ptr_->ptr_->obj_.ptr_);
  ASSERT_TRUE(handle.obj_pool_ == ptr_hdl.ptr_->ptr_->obj_.pool_);

  ret = t3m_.get_tablet(WashTabletPriority::WTP_HIGH, key, handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_TRUE(handle.is_valid());

  tablet = handle.get_obj();
  ASSERT_TRUE(nullptr != tablet);
  ASSERT_TRUE(tablet->pointer_hdl_.is_valid());

  ObIMemtableMgr *memtable_mgr = tablet->get_memtable_mgr();

  handle.reset();
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);

  ObTabletHandle old_handle;
  ret = t3m_.get_tablet(WashTabletPriority::WTP_HIGH, key, old_handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_TRUE(old_handle.is_valid());

  ret = t3m_.acquire_tablet(WashTabletPriority::WTP_HIGH, key, ls_handle, handle, false);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(2, t3m_.tablet_pool_.inner_used_num_);
  ASSERT_TRUE(handle.is_valid());

  tablet = handle.get_obj();
  ASSERT_TRUE(nullptr != tablet);
  ASSERT_TRUE(nullptr != tablet->get_memtable_mgr());
  ASSERT_TRUE(memtable_mgr == tablet->get_memtable_mgr());

  addr.first_id_ = 1;
  addr.second_id_ = 4;
  addr.offset_ = 0;
  addr.size_ = 4096;
  addr.type_ = ObMetaDiskAddr::DiskType::BLOCK;

  ret = t3m_.compare_and_swap_tablet(key, addr, old_handle, handle);
  ASSERT_EQ(common::OB_SUCCESS, ret);
  ASSERT_EQ(1, t3m_.tablet_map_.map_.size());
  ASSERT_EQ(2, t3m_.tablet_pool_.inner_used_num_);

  old_handle.reset();
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);

  handle.reset();
  ASSERT_EQ(1, t3m_.tablet_pool_.inner_used_num_);
}

TEST_F(TestTenantMetaMemMgr, test_multi_tablet)
{
  const int64_t thread_cnt = 16;
  ObTenantBase *tenant_base = MTL_CTX();
  TestConcurrentT3M multi_thread(thread_cnt, t3m_, tenant_base);

  int ret = multi_thread.start();
  ASSERT_EQ(OB_SUCCESS, ret);
  multi_thread.wait();
}

TEST_F(TestTenantMetaMemMgr, test_last_min_sstable_set)
{
  ObTableHandleV2 table_handle;
  ASSERT_EQ(OB_SUCCESS, t3m_.acquire_sstable(table_handle));
  ASSERT_TRUE(nullptr != table_handle.get_table());
  ObITable *table = static_cast<ObITable *>(table_handle.get_table());
  table->key_.tablet_id_ = 1;
  table->key_.scn_range_.start_scn_.set_base();
  table->key_.scn_range_.end_scn_.convert_for_tx(100);
  table->key_.table_type_ = ObITable::TableType::REMOTE_LOGICAL_MINOR_SSTABLE;

  ASSERT_EQ(OB_SUCCESS, t3m_.record_min_minor_sstable(ls_id_, table_handle));
  ASSERT_EQ(1, t3m_.last_min_minor_sstable_set_.size());

  table->key_.scn_range_.end_scn_.convert_for_tx(400);
  ASSERT_EQ(OB_SUCCESS, t3m_.record_min_minor_sstable(ls_id_, table_handle));
  ASSERT_EQ(1, t3m_.last_min_minor_sstable_set_.size());
}

TEST_F(TestTenantMetaMemMgr, test_tablet_wash_priority)
{
  ObTabletHandle handle;

  ASSERT_TRUE(handle.calc_wash_score(handle.wash_priority_) == INT64_MIN);

  handle.set_wash_priority(WashTabletPriority::WTP_HIGH);
  ASSERT_TRUE(handle.calc_wash_score(handle.wash_priority_) > 0);

  handle.set_wash_priority(WashTabletPriority::WTP_LOW);
  ASSERT_TRUE(handle.calc_wash_score(handle.wash_priority_) < 0);

  ObTabletHandle high_priority_handle;
  high_priority_handle.set_wash_priority(WashTabletPriority::WTP_HIGH);

  ASSERT_TRUE(high_priority_handle.calc_wash_score(high_priority_handle.wash_priority_) > handle.calc_wash_score(handle.wash_priority_));

  ObTabletHandle handle1(handle);
  ASSERT_TRUE(handle1.calc_wash_score(handle1.wash_priority_) < 0);

  ObTabletHandle handle2;
  ASSERT_TRUE(handle2.calc_wash_score(handle2.wash_priority_) == INT64_MIN);
  handle2 = high_priority_handle;
  ASSERT_TRUE(handle2.calc_wash_score(handle2.wash_priority_) > 0);
}

TEST_F(TestTenantMetaMemMgr, test_table_gc)
{
  // check the pool_arr of t3m
  ObITable::TableType type;
  for (type = ObITable::TableType::DATA_MEMTABLE; type <= ObITable::TableType::LOCK_MEMTABLE; type = (ObITable::TableType)(type + 1)) {
    ASSERT_NE(nullptr, t3m_.pool_arr_[static_cast<int>(type)]);
  }
  for (type = ObITable::TableType::MAJOR_SSTABLE; type < ObITable::TableType::MAX_TABLE_TYPE; type = (ObITable::TableType)(type + 1)) {
    ASSERT_NE(nullptr, t3m_.pool_arr_[static_cast<int>(type)]);
  }
  // check the count of tables in every pool before acquirement
  ASSERT_EQ(0, t3m_.memtable_pool_.inner_used_num_);
  ASSERT_EQ(0, t3m_.sstable_pool_.inner_used_num_);
  ASSERT_EQ(0, t3m_.tx_data_memtable_pool_.inner_used_num_);
  ASSERT_EQ(0, t3m_.tx_ctx_memtable_pool_.inner_used_num_);
  ASSERT_EQ(0, t3m_.lock_memtable_pool_.inner_used_num_);

  // acquire tables and set table_type
  ObTableHandleV2 sstable_handle;
  ObTableHandleV2 memtable_handle;
  ObTableHandleV2 tx_data_memtable_handle;
  ObTableHandleV2 tx_ctx_memtable_handle;
  ObTableHandleV2 lock_memtable_handle;
  ASSERT_EQ(OB_SUCCESS, t3m_.acquire_sstable(sstable_handle));
  ASSERT_EQ(OB_SUCCESS, t3m_.acquire_memtable(memtable_handle));
  ASSERT_EQ(OB_SUCCESS, t3m_.acquire_tx_data_memtable(tx_data_memtable_handle));
  ASSERT_EQ(OB_SUCCESS, t3m_.acquire_tx_ctx_memtable(tx_ctx_memtable_handle));
  ASSERT_EQ(OB_SUCCESS, t3m_.acquire_lock_memtable(lock_memtable_handle));
  sstable_handle.table_->set_table_type(ObITable::TableType::MAJOR_SSTABLE);
  memtable_handle.table_->set_table_type(ObITable::TableType::DATA_MEMTABLE);
  tx_data_memtable_handle.table_->set_table_type(ObITable::TableType::TX_DATA_MEMTABLE);
  tx_ctx_memtable_handle.table_->set_table_type(ObITable::TableType::TX_CTX_MEMTABLE);
  lock_memtable_handle.table_->set_table_type(ObITable::TableType::LOCK_MEMTABLE);

  // check the count of tables in every pool before reset
  ASSERT_EQ(1, t3m_.memtable_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.sstable_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.tx_data_memtable_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.tx_ctx_memtable_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.lock_memtable_pool_.inner_used_num_);

  // reset all handles
  sstable_handle.reset();
  memtable_handle.reset();
  tx_data_memtable_handle.reset();
  tx_ctx_memtable_handle.reset();
  lock_memtable_handle.reset();

  // check the count of tables in every pool after reset
  ASSERT_EQ(1, t3m_.memtable_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.sstable_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.tx_data_memtable_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.tx_ctx_memtable_pool_.inner_used_num_);
  ASSERT_EQ(1, t3m_.lock_memtable_pool_.inner_used_num_);

  // check the count of tables in free queue
  ASSERT_EQ(5, t3m_.free_tables_queue_.size());

  // recycle all tables
  bool all_table_cleaned = false; // no use
  ASSERT_EQ(OB_SUCCESS, t3m_.gc_tables_in_queue(all_table_cleaned));

  // check the count after gc
  ASSERT_EQ(0, t3m_.memtable_pool_.inner_used_num_);
  ASSERT_EQ(0, t3m_.sstable_pool_.inner_used_num_);
  ASSERT_EQ(0, t3m_.tx_data_memtable_pool_.inner_used_num_);
  ASSERT_EQ(0, t3m_.tx_ctx_memtable_pool_.inner_used_num_);
  ASSERT_EQ(0, t3m_.lock_memtable_pool_.inner_used_num_);
}

TEST_F(TestTenantMetaMemMgr, test_heap)
{
  int ret = OB_SUCCESS;
  int cmp_ret = OB_SUCCESS;
  ObTenantMetaMemMgr::HeapCompare heap_compare(cmp_ret);
  ObArenaAllocator allocator;
  ObTenantMetaMemMgr::Heap heap(heap_compare, &allocator);

  ObTenantMetaMemMgr::CandidateTabletInfo info_1;
  info_1.ls_id_ = 1;
  info_1.tablet_id_ = 1;
  info_1.wash_score_ = 200;

  ObTenantMetaMemMgr::CandidateTabletInfo info_2;
  info_1.ls_id_ = 2;
  info_1.tablet_id_ = 2;
  info_1.wash_score_ = 100;

  ObTenantMetaMemMgr::CandidateTabletInfo tmp;

  ret = heap.push(info_1);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(1, heap.count());

  tmp = heap.top();
  ASSERT_EQ(tmp.ls_id_, info_1.ls_id_);
  ASSERT_EQ(tmp.tablet_id_, info_1.tablet_id_);
  ASSERT_EQ(tmp.wash_score_, info_1.wash_score_);

  ret = heap.push(info_2);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(2, heap.count());

  tmp = heap.top();
  ASSERT_EQ(tmp.ls_id_, info_2.ls_id_);
  ASSERT_EQ(tmp.tablet_id_, info_2.tablet_id_);
  ASSERT_EQ(tmp.wash_score_, info_2.wash_score_);

  ret = heap.pop();
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(1, heap.count());

  tmp = heap.top();
  ASSERT_EQ(tmp.ls_id_, info_1.ls_id_);
  ASSERT_EQ(tmp.tablet_id_, info_1.tablet_id_);
  ASSERT_EQ(tmp.wash_score_, info_1.wash_score_);

  ret = heap.pop();
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(0, heap.count());
}
} // end namespace storage
} // end namespace oceanbase

int main(int argc, char **argv)
{
  system("rm -f test_tenant_meta_mem_mgr.log*");
  OB_LOGGER.set_file_name("test_tenant_meta_mem_mgr.log", true);
  OB_LOGGER.set_log_level("INFO");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

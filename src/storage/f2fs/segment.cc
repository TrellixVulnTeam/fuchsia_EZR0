// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <safemath/checked_math.h>

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

int UpdateNatsInCursum(SummaryBlock *raw_summary, int i) {
  int n_nats = NatsInCursum(raw_summary);
  raw_summary->n_nats = CpuToLe(safemath::checked_cast<uint16_t>(n_nats + i));
  return n_nats;
}

static int UpdateSitsInCursum(SummaryBlock *raw_summary, int i) {
  int n_sits = SitsInCursum(raw_summary);
  raw_summary->n_sits = CpuToLe(safemath::checked_cast<uint16_t>(n_sits + i));
  return n_sits;
}

SegmentEntry &SegmentManager::GetSegmentEntry(uint32_t segno) { return sit_info_->sentries[segno]; }

SectionEntry *SegmentManager::GetSectionEntry(uint32_t segno) {
  return &sit_info_->sec_entries[GetSecNo(segno)];
}

uint32_t SegmentManager::GetValidBlocks(uint32_t segno, int section) {
  // In order to get # of valid blocks in a section instantly from many
  // segments, f2fs manages two counting structures separately.
  if (section > 1) {
    return GetSectionEntry(segno)->valid_blocks;
  }
  return GetSegmentEntry(segno).valid_blocks;
}

void SegmentManager::SegInfoFromRawSit(SegmentEntry &segment_entry, SitEntry &raw_sit) {
  segment_entry.valid_blocks = GetSitVblocks(raw_sit);
  segment_entry.ckpt_valid_blocks = GetSitVblocks(raw_sit);
  memcpy(segment_entry.cur_valid_map.get(), raw_sit.valid_map, kSitVBlockMapSize);
  memcpy(segment_entry.ckpt_valid_map.get(), raw_sit.valid_map, kSitVBlockMapSize);
  segment_entry.type = GetSitType(raw_sit);
  segment_entry.mtime = LeToCpu(uint64_t{raw_sit.mtime});
}

void SegmentManager::SegInfoToRawSit(SegmentEntry &segment_entry, SitEntry &raw_sit) {
  uint16_t raw_vblocks =
      static_cast<uint16_t>(segment_entry.type << kSitVblocksShift) | segment_entry.valid_blocks;
  raw_sit.vblocks = CpuToLe(raw_vblocks);
  memcpy(raw_sit.valid_map, segment_entry.cur_valid_map.get(), kSitVBlockMapSize);
  memcpy(segment_entry.ckpt_valid_map.get(), raw_sit.valid_map, kSitVBlockMapSize);
  segment_entry.ckpt_valid_blocks = segment_entry.valid_blocks;
  raw_sit.mtime = CpuToLe(static_cast<uint64_t>(segment_entry.mtime));
}

uint32_t SegmentManager::FindNextInuse(uint32_t max, uint32_t segno) {
  uint32_t ret;
  fs::SharedLock segmap_lock(free_info_->segmap_lock);
  ret = FindNextBit(free_info_->free_segmap.get(), max, segno);
  return ret;
}

void SegmentManager::SetFree(uint32_t segno) {
  uint32_t secno = segno / superblock_info_->GetSegsPerSec();
  uint32_t start_segno = secno * superblock_info_->GetSegsPerSec();
  uint32_t next;

#ifdef __Fuchsia__
  std::lock_guard segmap_lock(free_info_->segmap_lock);
#endif  // __Fuchsia__
  ClearBit(segno, free_info_->free_segmap.get());
  ++free_info_->free_segments;

  next = FindNextBit(free_info_->free_segmap.get(), TotalSegs(), start_segno);
  if (next >= start_segno + superblock_info_->GetSegsPerSec()) {
    ClearBit(secno, free_info_->free_secmap.get());
    ++free_info_->free_sections;
  }
}

void SegmentManager::SetInuse(uint32_t segno) {
  uint32_t secno = segno / superblock_info_->GetSegsPerSec();
  SetBit(segno, free_info_->free_segmap.get());
  --free_info_->free_segments;
  if (!TestAndSetBit(secno, free_info_->free_secmap.get())) {
    --free_info_->free_sections;
  }
}

void SegmentManager::SetTestAndFree(uint32_t segno) {
  uint32_t secno = segno / superblock_info_->GetSegsPerSec();
  uint32_t start_segno = secno * superblock_info_->GetSegsPerSec();
  uint32_t next;

#ifdef __Fuchsia__
  std::lock_guard segmap_lock(free_info_->segmap_lock);
#endif  // __Fuchsia__
  if (TestAndClearBit(segno, free_info_->free_segmap.get())) {
    ++free_info_->free_segments;

    next = FindNextBit(free_info_->free_segmap.get(), TotalSegs(), start_segno);
    if (next >= start_segno + superblock_info_->GetSegsPerSec()) {
      if (TestAndClearBit(secno, free_info_->free_secmap.get()))
        ++free_info_->free_sections;
    }
  }
}

void SegmentManager::SetTestAndInuse(uint32_t segno) {
  uint32_t secno = segno / superblock_info_->GetSegsPerSec();
#ifdef __Fuchsia__
  std::lock_guard segmap_lock(free_info_->segmap_lock);
#endif  // __Fuchsia__
  if (!TestAndSetBit(segno, free_info_->free_segmap.get())) {
    --free_info_->free_segments;
    if (!TestAndSetBit(secno, free_info_->free_secmap.get())) {
      --free_info_->free_sections;
    }
  }
}

void SegmentManager::GetSitBitmap(void *dst_addr) {
  memcpy(dst_addr, sit_info_->sit_bitmap.get(), sit_info_->bitmap_size);
}

#if 0  // porting needed
block_t SegmentManager::WrittenBlockCount() {
  SuperblockInfo &superblock_info = fs_->GetSuperblockInfo();
  SitInfo *sit_i = GetSitInfo(&superblock_info);
  block_t vblocks;

  mtx_lock(&sit_i->sentry_lock);
  vblocks = sit_i->written_valid_blocks;
  mtx_unlock(&sit_i->sentry_lock);

  return vblocks;
}
#endif

block_t SegmentManager::FreeSegments() {
  fs::SharedLock segmap_lock(free_info_->segmap_lock);
  block_t free_segs = free_info_->free_segments;
  return free_segs;
}

block_t SegmentManager::FreeSections() {
  fs::SharedLock segmap_lock(free_info_->segmap_lock);
  block_t free_secs = free_info_->free_sections;
  return free_secs;
}

block_t SegmentManager::PrefreeSegments() {
  return dirty_info_->nr_dirty[static_cast<int>(DirtyType::kPre)];
}

block_t SegmentManager::DirtySegments() {
  return dirty_info_->nr_dirty[static_cast<int>(DirtyType::kDirtyHotData)] +
         dirty_info_->nr_dirty[static_cast<int>(DirtyType::kDirtyWarmData)] +
         dirty_info_->nr_dirty[static_cast<int>(DirtyType::kDirtyColdData)] +
         dirty_info_->nr_dirty[static_cast<int>(DirtyType::kDirtyHotNode)] +
         dirty_info_->nr_dirty[static_cast<int>(DirtyType::kDirtyWarmNode)] +
         dirty_info_->nr_dirty[static_cast<int>(DirtyType::kDirtyColdNode)];
}

block_t SegmentManager::OverprovisionSections() {
  return GetOPSegmentsCount() / superblock_info_->GetSegsPerSec();
}

block_t SegmentManager::ReservedSections() {
  return GetReservedSegmentsCount() / superblock_info_->GetSegsPerSec();
}
bool SegmentManager::NeedSSR() {
#ifdef F2FS_FORCE_SSR
  return true;
#else
  // TODO: need to consider allocation mode and gc mode
  return (FreeSections() < static_cast<uint32_t>(OverprovisionSections()));
#endif
}

int SegmentManager::GetSsrSegment(CursegType type) {
  CursegInfo *curseg = CURSEG_I(type);
  return GetVictimByDefault(GcType::kBgGc, type, AllocMode::kSSR, &(curseg->next_segno));
}

bool SegmentManager::HasNotEnoughFreeSecs() {
  return FreeSections() <= static_cast<uint32_t>(ReservedSections());
}

uint32_t SegmentManager::Utilization() {
  return static_cast<uint32_t>(static_cast<int64_t>(fs_->ValidUserBlocks()) * 100 /
                               static_cast<int64_t>(superblock_info_->GetUserBlockCount()));
}

// Sometimes f2fs may be better to drop out-of-place update policy.
// So, if fs utilization is over kMinIpuUtil, then f2fs tries to write
// data in the original place likewise other traditional file systems.
// Currently set 0 in percentage, which means that f2fs always uses ipu.
// It needs to be changed when gc is available.
constexpr uint32_t kMinIpuUtil = 0;
bool SegmentManager::NeedInplaceUpdate(VnodeF2fs *vnode) {
  if (vnode->IsDir())
    return false;
  if (NeedSSR() && Utilization() > kMinIpuUtil)
    return true;
  return false;
}

uint32_t SegmentManager::CursegSegno(int type) {
  CursegInfo *curseg = CURSEG_I(static_cast<CursegType>(type));
  return curseg->segno;
}

uint8_t SegmentManager::CursegAllocType(int type) {
  CursegInfo *curseg = CURSEG_I(static_cast<CursegType>(type));
  return curseg->alloc_type;
}

uint16_t SegmentManager::CursegBlkoff(int type) {
  CursegInfo *curseg = CURSEG_I(static_cast<CursegType>(type));
  return curseg->next_blkoff;
}

void SegmentManager::CheckSegRange(uint32_t segno) { ZX_ASSERT(segno < segment_count_); }

#if 0  // porting needed
// This function is used for only debugging.
// NOTE: In future, we have to remove this function.
void SegmentManager::VerifyBlockAddr(block_t blk_addr) {
  SuperblockInfo &superblock_info = fs_->GetSuperblockInfo();
  SmInfo *sm_info = GetSmInfo(&superblock_info);
  block_t total_blks = sm_info->segment_count << superblock_info.GetLogBlocksPerSeg();
  [[maybe_unused]] block_t start_addr = sm_info->seg0_blkaddr;
  [[maybe_unused]] block_t end_addr = start_addr + total_blks - 1;
  ZX_ASSERT(!(blk_addr < start_addr));
  ZX_ASSERT(!(blk_addr > end_addr));
}
#endif

// Summary block is always treated as invalid block
void SegmentManager::CheckBlockCount(int segno, SitEntry &raw_sit) {
  uint32_t end_segno = segment_count_ - 1;
  int valid_blocks = 0;
  uint32_t i;

  // check segment usage
  ZX_ASSERT(!(GetSitVblocks(raw_sit) > superblock_info_->GetBlocksPerSeg()));

  // check boundary of a given segment number
  ZX_ASSERT(!(segno > (int)end_segno));

  // check bitmap with valid block count
  for (i = 0; i < superblock_info_->GetBlocksPerSeg(); ++i) {
    if (TestValidBitmap(i, raw_sit.valid_map))
      ++valid_blocks;
  }
  ZX_ASSERT(GetSitVblocks(raw_sit) == valid_blocks);
}

pgoff_t SegmentManager::CurrentSitAddr(uint32_t start) {
  uint32_t offset = SitBlockOffset(start);
  block_t blk_addr = sit_info_->sit_base_addr + offset;

  CheckSegRange(start);

  // calculate sit block address
  if (TestValidBitmap(offset, sit_info_->sit_bitmap.get()))
    blk_addr += sit_info_->sit_blocks;

  return blk_addr;
}

pgoff_t SegmentManager::NextSitAddr(pgoff_t block_addr) {
  block_addr -= sit_info_->sit_base_addr;
  if (block_addr < sit_info_->sit_blocks)
    block_addr += sit_info_->sit_blocks;
  else
    block_addr -= sit_info_->sit_blocks;

  return block_addr + sit_info_->sit_base_addr;
}

void SegmentManager::SetToNextSit(uint32_t start) {
  uint32_t block_off = SitBlockOffset(start);

  if (TestValidBitmap(block_off, sit_info_->sit_bitmap.get())) {
    ClearValidBitmap(block_off, sit_info_->sit_bitmap.get());
  } else {
    SetValidBitmap(block_off, sit_info_->sit_bitmap.get());
  }
}

uint64_t SegmentManager::GetMtime() {
  auto cur_time = time(nullptr);
  return sit_info_->elapsed_time + cur_time - sit_info_->mounted_time;
}

void SegmentManager::SetSummary(Summary *sum, nid_t nid, uint32_t ofs_in_node, uint8_t version) {
  sum->nid = CpuToLe(nid);
  sum->ofs_in_node = CpuToLe(static_cast<uint16_t>(ofs_in_node));
  sum->version = version;
}

block_t SegmentManager::StartSumBlock() {
  return superblock_info_->StartCpAddr() +
         LeToCpu(superblock_info_->GetCheckpoint().cp_pack_start_sum);
}

block_t SegmentManager::SumBlkAddr(int base, int type) {
  return superblock_info_->StartCpAddr() +
         LeToCpu(superblock_info_->GetCheckpoint().cp_pack_total_block_count) - (base + 1) + type;
}

SegmentManager::SegmentManager(F2fs *fs) : fs_(fs) { superblock_info_ = &fs->GetSuperblockInfo(); };

bool SegmentManager::NeedToFlush() {
  uint32_t pages_per_sec =
      (1 << superblock_info_->GetLogBlocksPerSeg()) * superblock_info_->GetSegsPerSec();
  int node_secs = ((superblock_info_->GetPageCount(CountType::kDirtyNodes) + pages_per_sec - 1) >>
                   superblock_info_->GetLogBlocksPerSeg()) /
                  superblock_info_->GetSegsPerSec();
  int dent_secs = ((superblock_info_->GetPageCount(CountType::kDirtyDents) + pages_per_sec - 1) >>
                   superblock_info_->GetLogBlocksPerSeg()) /
                  superblock_info_->GetSegsPerSec();

  if (superblock_info_->IsOnRecovery())
    return false;

  return FreeSections() <= static_cast<uint32_t>(node_secs + 2 * dent_secs + ReservedSections());
}

// This function balances dirty node and dentry pages.
// In addition, it controls garbage collection.
void SegmentManager::BalanceFs() {
  WritebackControl wbc = {
#if 0  // porting needed
      // .nr_to_write = LONG_MAX,
      // .sync_mode = WB_SYNC_ALL,
      // .for_reclaim = 0,
#endif
  };

  if (superblock_info_->IsOnRecovery())
    return;

  // We should do checkpoint when there are so many dirty node pages
  // with enough free segments. After then, we should do GC.
  if (NeedToFlush()) {
    fs_->SyncDirtyDirInodes();
    fs_->GetNodeManager().SyncNodePages(0, &wbc);
  }

  // TODO: need to change after gc IMPL
  // Without GC, f2fs needs to secure free segments aggressively.
  if (/*HasNotEnoughFreeSecs() &&*/ PrefreeSegments()) {
#if 0  // porting needed
    // mtx_lock(&superblock_info.gc_mutex);
    // F2fsGc(&superblock_info, 1);
#endif
    fs_->WriteCheckpoint(false, false);
  }
}

void SegmentManager::LocateDirtySegment(uint32_t segno, DirtyType dirty_type) {
  // need not be added
  if (IsCurSeg(segno)) {
    return;
  }

  if (!TestAndSetBit(segno, dirty_info_->dirty_segmap[static_cast<int>(dirty_type)].get()))
    ++dirty_info_->nr_dirty[static_cast<int>(dirty_type)];

  if (dirty_type == DirtyType::kDirty) {
    SegmentEntry &sentry = GetSegmentEntry(segno);
    dirty_type = static_cast<DirtyType>(sentry.type);
    if (!TestAndSetBit(segno, dirty_info_->dirty_segmap[static_cast<int>(dirty_type)].get()))
      ++dirty_info_->nr_dirty[static_cast<int>(dirty_type)];
  }
}

void SegmentManager::RemoveDirtySegment(uint32_t segno, DirtyType dirty_type) {
  if (TestAndClearBit(segno, dirty_info_->dirty_segmap[static_cast<int>(dirty_type)].get())) {
    --dirty_info_->nr_dirty[static_cast<int>(dirty_type)];
  }

  if (dirty_type == DirtyType::kDirty) {
    SegmentEntry &sentry = GetSegmentEntry(segno);
    dirty_type = static_cast<DirtyType>(sentry.type);
    if (TestAndClearBit(segno, dirty_info_->dirty_segmap[static_cast<int>(dirty_type)].get())) {
      --dirty_info_->nr_dirty[static_cast<int>(dirty_type)];
    }
    ClearBit(segno, dirty_info_->victim_segmap[static_cast<int>(GcType::kFgGc)].get());
    ClearBit(segno, dirty_info_->victim_segmap[static_cast<int>(GcType::kBgGc)].get());
  }
}

// Should not occur error such as ZX_ERR_NO_MEMORY.
// Adding dirty entry into seglist is not critical operation.
// If a given segment is one of current working segments, it won't be added.
void SegmentManager::LocateDirtySegment(uint32_t segno) {
  uint32_t valid_blocks;

  if (segno == kNullSegNo || IsCurSeg(segno))
    return;

#ifdef __Fuchsia__
  fbl::AutoLock seglist_lock(&dirty_info_->seglist_lock);
#endif  // __Fuchsia__

  valid_blocks = GetValidBlocks(segno, 0);

  if (valid_blocks == 0) {
    LocateDirtySegment(segno, DirtyType::kPre);
    RemoveDirtySegment(segno, DirtyType::kDirty);
  } else if (valid_blocks < superblock_info_->GetBlocksPerSeg()) {
    LocateDirtySegment(segno, DirtyType::kDirty);
  } else {
    // Recovery routine with SSR needs this
    RemoveDirtySegment(segno, DirtyType::kDirty);
  }
}

// Should call clear_prefree_segments after checkpoint is done.
void SegmentManager::SetPrefreeAsFreeSegments() {
  uint32_t segno, offset = 0;
  uint32_t total_segs = TotalSegs();

#ifdef __Fuchsia__
  fbl::AutoLock seglist_lock(&dirty_info_->seglist_lock);
#endif  // __Fuchsia__

  while (true) {
    segno = FindNextBit(dirty_info_->dirty_segmap[static_cast<int>(DirtyType::kPre)].get(),
                        total_segs, offset);
    if (segno >= total_segs)
      break;
    SetTestAndFree(segno);
    offset = segno + 1;
  }
}

void SegmentManager::ClearPrefreeSegments() {
  uint32_t segno, offset = 0;
  uint32_t total_segs = TotalSegs();

#ifdef __Fuchsia__
  fbl::AutoLock seglist_lock(&dirty_info_->seglist_lock);
#endif  // __Fuchsia__
  while (true) {
    segno = FindNextBit(dirty_info_->dirty_segmap[static_cast<int>(DirtyType::kPre)].get(),
                        total_segs, offset);
    if (segno >= total_segs)
      break;

    offset = segno + 1;
    if (TestAndClearBit(segno,
                        dirty_info_->dirty_segmap[static_cast<int>(DirtyType::kPre)].get())) {
      --dirty_info_->nr_dirty[static_cast<int>(DirtyType::kPre)];
    }

    if (superblock_info_->TestOpt(kMountDiscard))
      fs_->GetBc().Trim(StartBlock(segno), (1 << superblock_info_->GetLogBlocksPerSeg()));
  }
}

void SegmentManager::MarkSitEntryDirty(uint32_t segno) {
  if (!TestAndSetBit(segno, sit_info_->dirty_sentries_bitmap.get()))
    ++sit_info_->dirty_sentries;
}

void SegmentManager::SetSitEntryType(CursegType type, uint32_t segno, int modified) {
  SegmentEntry &segment_entry = GetSegmentEntry(segno);
  segment_entry.type = static_cast<uint8_t>(type);
  if (modified)
    MarkSitEntryDirty(segno);
}

void SegmentManager::UpdateSitEntry(block_t blkaddr, int del) {
  uint32_t offset;
  uint64_t new_vblocks;
  uint32_t segno = GetSegmentNumber(blkaddr);
  SegmentEntry &segment_entry = GetSegmentEntry(segno);

  new_vblocks = segment_entry.valid_blocks + del;
  offset = GetSegOffFromSeg0(blkaddr) & (superblock_info_->GetBlocksPerSeg() - 1);

  ZX_ASSERT(!((new_vblocks >> (sizeof(uint16_t) << 3) ||
               (new_vblocks > superblock_info_->GetBlocksPerSeg()))));

  segment_entry.valid_blocks = static_cast<uint16_t>(new_vblocks);
  segment_entry.mtime = GetMtime();
  sit_info_->max_mtime = segment_entry.mtime;

  // Update valid block bitmap
  if (del > 0) {
    if (SetValidBitmap(offset, segment_entry.cur_valid_map.get()))
      ZX_ASSERT(0);
  } else {
    if (!ClearValidBitmap(offset, segment_entry.cur_valid_map.get()))
      ZX_ASSERT(0);
  }
  if (!TestValidBitmap(offset, segment_entry.ckpt_valid_map.get()))
    segment_entry.ckpt_valid_blocks += del;

  MarkSitEntryDirty(segno);

  // update total number of valid blocks to be written in ckpt area
  sit_info_->written_valid_blocks += del;

  if (superblock_info_->GetSegsPerSec() > 1)
    GetSectionEntry(segno)->valid_blocks += del;
}

void SegmentManager::RefreshSitEntry(block_t old_blkaddr, block_t new_blkaddr) {
  UpdateSitEntry(new_blkaddr, 1);
  if (GetSegmentNumber(old_blkaddr) != kNullSegNo)
    UpdateSitEntry(old_blkaddr, -1);
}

void SegmentManager::InvalidateBlocks(block_t addr) {
  uint32_t segno = GetSegmentNumber(addr);

  ZX_ASSERT(addr != kNullAddr);
  if (addr == kNewAddr)
    return;

#ifdef __Fuchsia__
  fbl::AutoLock sentry_lock(&sit_info_->sentry_lock);
#endif  // __Fuchsia__

  // add it into sit main buffer
  UpdateSitEntry(addr, -1);

  // add it into dirty seglist
  LocateDirtySegment(segno);
}

// This function should be resided under the curseg_mutex lock
void SegmentManager::AddSumEntry(CursegType type, Summary *sum, uint16_t offset) {
  CursegInfo *curseg = CURSEG_I(type);
  char *addr = reinterpret_cast<char *>(curseg->sum_blk);
  (addr) += offset * sizeof(Summary);
  memcpy(addr, sum, sizeof(Summary));
}

// Calculate the number of current summary pages for writing
int SegmentManager::NpagesForSummaryFlush() {
  SuperblockInfo &superblock_info = fs_->GetSuperblockInfo();
  int total_size_bytes = 0;
  int valid_sum_count = 0;
  int i, sum_space;

  for (i = static_cast<int>(CursegType::kCursegHotData);
       i <= static_cast<int>(CursegType::kCursegColdData); ++i) {
    if (superblock_info.GetCheckpoint().alloc_type[i] == static_cast<uint8_t>(AllocMode::kSSR)) {
      valid_sum_count += superblock_info.GetBlocksPerSeg();
    } else {
      valid_sum_count += CursegBlkoff(i);
    }
  }

  total_size_bytes =
      valid_sum_count * (kSummarySize + 1) + sizeof(NatJournal) + 2 + sizeof(SitJournal) + 2;
  sum_space = kPageCacheSize - kSumFooterSize;
  if (total_size_bytes < sum_space) {
    return 1;
  } else if (total_size_bytes < 2 * sum_space) {
    return 2;
  }
  return 3;
}

// Caller should put this summary page
Page *SegmentManager::GetSumPage(uint32_t segno) { return fs_->GetMetaPage(GetSumBlock(segno)); }

void SegmentManager::WriteSumPage(SummaryBlock *sum_blk, block_t blk_addr) {
  Page *page = fs_->GrabMetaPage(blk_addr);
  void *kaddr = PageAddress(page);
  memcpy(kaddr, sum_blk, kPageCacheSize);
#if 0  // porting needed
  // set_page_dirty(page);
#endif
  FlushDirtyMetaPage(fs_, page);
  F2fsPutPage(page, 1);
}

// Find a new segment from the free segments bitmap to right order
// This function should be returned with success, otherwise BUG
void SegmentManager::GetNewSegment(uint32_t *newseg, bool new_sec, int dir) {
  SuperblockInfo &superblock_info = fs_->GetSuperblockInfo();
  uint32_t total_secs = superblock_info.GetTotalSections();
  uint32_t segno, secno, zoneno;
  uint32_t total_zones = superblock_info.GetTotalSections() / superblock_info.GetSecsPerZone();
  uint32_t hint = *newseg / superblock_info.GetSegsPerSec();
  uint32_t old_zoneno = GetZoneNoFromSegNo(*newseg);
  uint32_t left_start = hint;
  bool init = true;
  int go_left = 0;
  int i;
  bool got_it = false;

#ifdef __Fuchsia__
  std::lock_guard segmap_lock(free_info_->segmap_lock);
#endif  // __Fuchsia__

  auto find_other_zone = [&]() -> bool {
    secno = FindNextZeroBit(free_info_->free_secmap.get(), total_secs, hint);
    if (secno >= total_secs) {
      if (dir == static_cast<int>(AllocDirection::kAllocRight)) {
        secno = FindNextZeroBit(free_info_->free_secmap.get(), total_secs, 0);
        ZX_ASSERT(!(secno >= total_secs));
      } else {
        go_left = 1;
        left_start = hint - 1;
      }
    }
    if (go_left == 0)
      return true;
    return false;
  };

  if (!new_sec && ((*newseg + 1) % superblock_info.GetSegsPerSec())) {
    segno = FindNextZeroBit(free_info_->free_segmap.get(), TotalSegs(), *newseg + 1);
    if (segno < TotalSegs()) {
      got_it = true;
    }
  }

  while (!got_it) {
    if (!find_other_zone()) {
      while (TestBit(left_start, free_info_->free_secmap.get())) {
        if (left_start > 0) {
          --left_start;
          continue;
        }
        left_start = FindNextZeroBit(free_info_->free_secmap.get(), total_secs, 0);
        ZX_ASSERT(!(left_start >= total_secs));
        break;
      }
      secno = left_start;
    }

    hint = secno;
    segno = secno * superblock_info.GetSegsPerSec();
    zoneno = secno / superblock_info.GetSecsPerZone();

    // give up on finding another zone
    if (!init) {
      break;
    }
    if (superblock_info.GetSecsPerZone() == 1) {
      break;
    }
    if (zoneno == old_zoneno) {
      break;
    }
    if (dir == static_cast<int>(AllocDirection::kAllocLeft)) {
      if (!go_left && zoneno + 1 >= total_zones) {
        break;
      }
      if (go_left && zoneno == 0) {
        break;
      }
    }
    for (i = 0; i < kNrCursegType; ++i) {
      if (CURSEG_I(static_cast<CursegType>(i))->zone == zoneno) {
        break;
      }
    }

    if (i < kNrCursegType) {
      // zone is in user, try another
      if (go_left) {
        hint = zoneno * superblock_info.GetSecsPerZone() - 1;
      } else if (zoneno + 1 >= total_zones) {
        hint = 0;
      } else {
        hint = (zoneno + 1) * superblock_info.GetSecsPerZone();
      }
      init = false;
      continue;
    }
    break;
  }
  // set it as dirty segment in free segmap
  ZX_ASSERT(!TestBit(segno, free_info_->free_segmap.get()));
  SetInuse(segno);
  *newseg = segno;
}

void SegmentManager::ResetCurseg(CursegType type, int modified) {
  CursegInfo *curseg = CURSEG_I(type);
  SummaryFooter *sum_footer;

  curseg->segno = curseg->next_segno;
  curseg->zone = GetZoneNoFromSegNo(curseg->segno);
  curseg->next_blkoff = 0;
  curseg->next_segno = kNullSegNo;

  sum_footer = &(curseg->sum_blk->footer);
  memset(sum_footer, 0, sizeof(SummaryFooter));
  if (IsDataSeg(type))
    SetSumType(sum_footer, kSumTypeData);
  if (IsNodeSeg(type))
    SetSumType(sum_footer, kSumTypeNode);
  SetSitEntryType(type, curseg->segno, modified);
}

// Allocate a current working segment.
// This function always allocates a free segment in LFS manner.
void SegmentManager::NewCurseg(CursegType type, bool new_sec) {
  SuperblockInfo &superblock_info = fs_->GetSuperblockInfo();
  CursegInfo *curseg = CURSEG_I(type);
  uint32_t segno = curseg->segno;
  int dir = static_cast<int>(AllocDirection::kAllocLeft);

  WriteSumPage(curseg->sum_blk, GetSumBlock(curseg->segno));
  if (type == CursegType::kCursegWarmData || type == CursegType::kCursegColdData)
    dir = static_cast<int>(AllocDirection::kAllocRight);

  if (superblock_info.TestOpt(kMountNoheap))
    dir = static_cast<int>(AllocDirection::kAllocRight);

  GetNewSegment(&segno, new_sec, dir);
  curseg->next_segno = segno;
  ResetCurseg(type, 1);
  curseg->alloc_type = static_cast<uint8_t>(AllocMode::kLFS);
}

void SegmentManager::NextFreeBlkoff(CursegInfo *seg, block_t start) {
  SuperblockInfo &superblock_info = fs_->GetSuperblockInfo();
  SegmentEntry &segment_entry = GetSegmentEntry(seg->segno);
  block_t ofs;
  for (ofs = start; ofs < superblock_info.GetBlocksPerSeg(); ++ofs) {
    if (!TestValidBitmap(ofs, segment_entry.ckpt_valid_map.get()) &&
        !TestValidBitmap(ofs, segment_entry.cur_valid_map.get()))
      break;
  }
  seg->next_blkoff = static_cast<uint16_t>(ofs);
}

// If a segment is written by LFS manner, next block offset is just obtained
// by increasing the current block offset. However, if a segment is written by
// SSR manner, next block offset obtained by calling __next_free_blkoff
void SegmentManager::RefreshNextBlkoff(CursegInfo *seg) {
  if (seg->alloc_type == static_cast<uint8_t>(AllocMode::kSSR)) {
    NextFreeBlkoff(seg, seg->next_blkoff + 1);
  } else {
    ++seg->next_blkoff;
  }
}

// This function always allocates a used segment (from dirty seglist) by SSR
// manner, so it should recover the existing segment information of valid blocks
void SegmentManager::ChangeCurseg(CursegType type, bool reuse) {
  CursegInfo *curseg = CURSEG_I(type);
  uint32_t new_segno = curseg->next_segno;
  SummaryBlock *sum_node;
  Page *sum_page = nullptr;

  WriteSumPage(curseg->sum_blk, GetSumBlock(curseg->segno));
  SetTestAndInuse(new_segno);

  {
#ifdef __Fuchsia__
    fbl::AutoLock seglist_lock(&dirty_info_->seglist_lock);
#endif  // __Fuchsia__
    RemoveDirtySegment(new_segno, DirtyType::kPre);
    RemoveDirtySegment(new_segno, DirtyType::kDirty);
  }

  ResetCurseg(type, 1);
  curseg->alloc_type = static_cast<uint8_t>(AllocMode::kSSR);
  NextFreeBlkoff(curseg, 0);

  if (reuse) {
    sum_page = GetSumPage(new_segno);
    sum_node = static_cast<SummaryBlock *>(PageAddress(sum_page));
    memcpy(curseg->sum_blk, sum_node, kSumEntrySize);
    F2fsPutPage(sum_page, 1);
  }
}

// flush out current segment and replace it with new segment
// This function should be returned with success, otherwise BUG
void SegmentManager::AllocateSegmentByDefault(CursegType type, bool force) {
  SuperblockInfo &superblock_info = fs_->GetSuperblockInfo();
  CursegInfo *curseg = CURSEG_I(type);
  // uint32_t ofs_unit;

  if (force) {
    NewCurseg(type, true);
  } else {
    // TODO: Temporarily enable ssr for warm node segments
    // when the kMountDisableRollForward bit is clear.
    // It is very helpful not to waste node segments in the current sync io impl.
    // Need to remove it after gc IMPL or cache.
    if (!superblock_info.TestOpt(kMountDisableRollForward) && type == CursegType::kCursegWarmNode) {
      NewCurseg(type, false);
    } else if (NeedSSR() && GetSsrSegment(type)) {
      ChangeCurseg(type, true);
    } else {
      NewCurseg(type, false);
    }
  }
  superblock_info.IncSegmentCount(curseg->alloc_type);
}

void SegmentManager::AllocateNewSegments() {
  CursegInfo *curseg;
  uint32_t old_curseg;
  int i;

  for (i = static_cast<int>(CursegType::kCursegHotData);
       i <= static_cast<int>(CursegType::kCursegColdData); ++i) {
    curseg = CURSEG_I(static_cast<CursegType>(i));
    old_curseg = curseg->segno;
    AllocateSegmentByDefault(static_cast<CursegType>(i), true);
    LocateDirtySegment(old_curseg);
  }
}

#if 0  // porting needed
const segment_allocation default_salloc_ops = {
        .allocate_segment = AllocateSegmentByDefault,
};

void SegmentManager::EndIoWrite(bio *bio, int err) {
  // const int uptodate = TestBit(BIO_UPTODATE, &bio->bi_flags);
  // bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1;
  // BioPrivate *p = bio->bi_private;

  // do {
  // 	page *page = bvec->bv_page;

  // 	if (--bvec >= bio->bi_io_vec)
  // 		prefetchw(&bvec->bv_page->flags);
  // 	if (!uptodate) {
  // 		SetPageError(page);
  // 		if (page->mapping)
  // 			SetBit(AS_EIO, &page->mapping->flags);
  // 		p->superblock_info->ckpt->ckpt_flags |= kCpErrorFlag;
  // 		set_page_dirty(page);
  // 	}
  // 	end_page_writeback(page);
  // 	dec_page_count(p->superblock_info, CountType::kWriteback);
  // } while (bvec >= bio->bi_io_vec);

  // if (p->is_sync)
  // 	complete(p->wait);
  // kfree(p);
  // bio_put(bio);
}

bio *SegmentManager::BioAlloc(block_device *bdev, sector_t first_sector, int nr_vecs,
                                 gfp_t gfp_flags) {
  // 	bio *bio;
  // repeat:
  // 	/* allocate new bio */
  // 	bio = bio_alloc(gfp_flags, nr_vecs);

  // 	if (bio == nullptr && (current->flags & PF_MEMALLOC)) {
  // 		while (!bio && (nr_vecs /= 2))
  // 			bio = bio_alloc(gfp_flags, nr_vecs);
  // 	}
  // 	if (bio) {
  // 		bio->bi_bdev = bdev;
  // 		bio->bi_sector = first_sector;
  // retry:
  // 		bio->bi_private = kmalloc(sizeof(BioPrivate),
  // 						GFP_NOFS | __GFP_HIGH);
  // 		if (!bio->bi_private) {
  // 			cond_resched();
  // 			goto retry;
  // 		}
  // 	}
  // 	if (bio == nullptr) {
  // 		cond_resched();
  // 		goto repeat;
  // 	}
  // 	return bio;
  return nullptr;
}

void SegmentManager::DoSubmitBio(PageType type, bool sync) {
  // int rw = sync ? kWriteSync : kWrite;
  // PageType btype = type > META ? META : type;

  // if (type >= PageType::kMetaFlush)
  // 	rw = kWriteFlushFua;

  // if (superblock_info->bio[btype]) {
  // 	BioPrivate *p = superblock_info->bio[btype]->bi_private;
  // 	p->superblock_info = superblock_info;
  // 	superblock_info->bio[btype]->bi_end_io = f2fs_end_io_write;
  // 	if (type == PageType::kMetaFlush) {
  // 		DECLARE_COMPLETION_ONSTACK(wait);
  // 		p->is_sync = true;
  // 		p->wait = &wait;
  // 		submit_bio(rw, superblock_info->bio[btype]);
  // 		wait_for_completion(&wait);
  // 	} else {
  // 		p->is_sync = false;
  // 		submit_bio(rw, superblock_info->bio[btype]);
  // 	}
  // 	superblock_info->bio[btype] = nullptr;
  // }
}

void SegmentManager::SubmitBio(PageType type, bool sync) {
  // down_write(&superblock_info->bio_sem);
  // DoSubmitBio(type, sync);
  // up_write(&superblock_info->bio_sem);
}
#endif

void SegmentManager::SubmitWritePage(Page *page, block_t blk_addr, PageType type) {
  zx_status_t ret = fs_->GetBc().Writeblk(blk_addr, page->data);
  if (ret) {
    FX_LOGS(ERROR) << "SubmitWritePage error " << ret;
  }
#if 0  // porting needed (bio)
  //	fs_->bc_->Flush();

  // 	block_device *bdev = superblock_info->sb->s_bdev;

  // 	verify_block_addr(superblock_info, blk_addr);

  // 	down_write(&superblock_info->bio_sem);

  // 	superblock_info->AddPageCount(CountType::kWriteback);

  // 	if (superblock_info->bio[type] && superblock_info->last_block_in_bio[type] != blk_addr - 1)
  // 		do_submit_bio(superblock_info, type, false);
  // alloc_new:
  // 	if (superblock_info->bio[type] == nullptr)
  // 		superblock_info->bio[type] = f2fs_bio_alloc(bdev,
  // 				blk_addr << (superblock_info->GetLogBlocksize() - 9),
  // 				bio_get_nr_vecs(bdev), GFP_NOFS | __GFP_HIGH);

  // 	if (bio_add_page(superblock_info->bio[type], page, kPageCacheSize, 0) <
  // 							kPageCacheSize) {
  // 		do_submit_bio(superblock_info, type, false);
  // 		goto alloc_new;
  // 	}

  // 	superblock_info->last_block_in_bio[type] = blk_addr;

  // 	up_write(&superblock_info->bio_sem);
#endif
}

bool SegmentManager::HasCursegSpace(CursegType type) {
  SuperblockInfo &superblock_info = fs_->GetSuperblockInfo();
  CursegInfo *curseg = CURSEG_I(type);
  if (curseg->next_blkoff < superblock_info.GetBlocksPerSeg()) {
    return true;
  }
  return false;
}

CursegType SegmentManager::GetSegmentType2(Page *page, PageType p_type) {
  if (p_type == PageType::kData) {
    return CursegType::kCursegHotData;
  } else {
    return CursegType::kCursegHotNode;
  }
}

CursegType SegmentManager::GetSegmentType4(Page *page, PageType p_type) {
  if (p_type == PageType::kData) {
    VnodeF2fs *vnode = static_cast<f2fs::VnodeF2fs *>(page->host);

    if (vnode->IsDir()) {
      return CursegType::kCursegHotData;
    }
    return CursegType::kCursegColdData;
  }

  if (NodeManager::IS_DNODE(*page) && !NodeManager::IsColdNode(*page)) {
    return CursegType::kCursegHotNode;
  }
  return CursegType::kCursegColdNode;
}

CursegType SegmentManager::GetSegmentType6(Page *page, PageType p_type) {
  if (p_type == PageType::kData) {
    VnodeF2fs *vnode = static_cast<f2fs::VnodeF2fs *>(page->host);

    if (vnode->IsDir()) {
      return CursegType::kCursegHotData;
    } else if (/*NodeManager::IsColdData(*page) ||*/ NodeManager::IsColdFile(*vnode)) {
      return CursegType::kCursegColdData;
    }
    return CursegType::kCursegWarmData;
  }

  if (NodeManager::IS_DNODE(*page)) {
    return NodeManager::IsColdNode(*page) ? CursegType::kCursegWarmNode
                                          : CursegType::kCursegHotNode;
  }
  return CursegType::kCursegColdNode;
}

CursegType SegmentManager::GetSegmentType(Page *page, PageType p_type) {
  SuperblockInfo &superblock_info = fs_->GetSuperblockInfo();
  switch (superblock_info.GetActiveLogs()) {
    case 2:
      return GetSegmentType2(page, p_type);
    case 4:
      return GetSegmentType4(page, p_type);
    case 6:
      return GetSegmentType6(page, p_type);
    default:
      ZX_ASSERT(0);
  }
}

void SegmentManager::DoWritePage(Page *page, block_t old_blkaddr, block_t *new_blkaddr,
                                 Summary *sum, PageType p_type) {
  CursegInfo *curseg;
  CursegType type;

  type = GetSegmentType(page, p_type);
  curseg = CURSEG_I(type);

  {
#ifdef __Fuchsia__
    fbl::AutoLock curseg_lock(&curseg->curseg_mutex);
#endif  // __Fuchsia__
    *new_blkaddr = NextFreeBlkAddr(type);

    // AddSumEntry should be resided under the curseg_mutex
    // because this function updates a summary entry in the
    // current summary block.
    AddSumEntry(type, sum, curseg->next_blkoff);

    {
#ifdef __Fuchsia__
      fbl::AutoLock sentry_lock(&sit_info_->sentry_lock);
#endif  // __Fuchsia__
      RefreshNextBlkoff(curseg);
      superblock_info_->IncBlockCount(curseg->alloc_type);

      // SIT information should be updated before segment allocation,
      // since SSR needs latest valid block information.
      RefreshSitEntry(old_blkaddr, *new_blkaddr);

      if (!HasCursegSpace(type)) {
        AllocateSegmentByDefault(type, false);
      }

      LocateDirtySegment(GetSegmentNumber(old_blkaddr));
      LocateDirtySegment(GetSegmentNumber(*new_blkaddr));
    }

    if (p_type == PageType::kNode)
      fs_->GetNodeManager().FillNodeFooterBlkaddr(page, NextFreeBlkAddr(type));
  }

  // writeout dirty page into bdev
  SubmitWritePage(page, *new_blkaddr, p_type);
}

zx_status_t SegmentManager::WriteMetaPage(Page *page, WritebackControl *wbc) {
#if 0  // porting needed
  // if (wbc && wbc->for_reclaim)
  // 	return kAopWritepageActivate;
#endif
  SetPageWriteback(page);
  SubmitWritePage(page, static_cast<block_t>(page->index), PageType::kMeta);
  return ZX_OK;
}

void SegmentManager::WriteNodePage(Page *page, uint32_t nid, block_t old_blkaddr,
                                   block_t *new_blkaddr) {
  Summary sum;
  SetSummary(&sum, nid, 0, 0);
  DoWritePage(page, old_blkaddr, new_blkaddr, &sum, PageType::kNode);
}

void SegmentManager::WriteDataPage(VnodeF2fs *vnode, Page *page, DnodeOfData *dn,
                                   block_t old_blkaddr, block_t *new_blkaddr) {
  Summary sum;
  NodeInfo ni;

  ZX_ASSERT(old_blkaddr != kNullAddr);
  fs_->GetNodeManager().GetNodeInfo(dn->nid, ni);
  SetSummary(&sum, dn->nid, dn->ofs_in_node, ni.version);

  DoWritePage(page, old_blkaddr, new_blkaddr, &sum, PageType::kData);
}

void SegmentManager::RewriteDataPage(Page *page, block_t old_blk_addr) {
  SubmitWritePage(page, old_blk_addr, PageType::kData);
}

void SegmentManager::RecoverDataPage(Page *page, Summary *sum, block_t old_blkaddr,
                                     block_t new_blkaddr) {
  CursegInfo *curseg;
  uint32_t old_cursegno;
  CursegType type;
  uint32_t segno = GetSegmentNumber(new_blkaddr);
  SegmentEntry &segment_entry = GetSegmentEntry(segno);

  type = static_cast<CursegType>(segment_entry.type);

  if (segment_entry.valid_blocks == 0 && !IsCurSeg(segno)) {
    if (old_blkaddr == kNullAddr) {
      type = CursegType::kCursegColdData;
    } else {
      type = CursegType::kCursegWarmData;
    }
  }
  curseg = CURSEG_I(type);

#ifdef __Fuchsia__
  fbl::AutoLock curseg_lock(&curseg->curseg_mutex);
  fbl::AutoLock sentry_lock(&sit_info_->sentry_lock);
#endif  // __Fuchsia__

  old_cursegno = curseg->segno;

  // change the current segment
  if (segno != curseg->segno) {
    curseg->next_segno = segno;
    ChangeCurseg(type, true);
  }

  curseg->next_blkoff = safemath::checked_cast<uint16_t>(GetSegOffFromSeg0(new_blkaddr) &
                                                         (superblock_info_->GetBlocksPerSeg() - 1));
  AddSumEntry(type, sum, curseg->next_blkoff);

  RefreshSitEntry(old_blkaddr, new_blkaddr);

  LocateDirtySegment(old_cursegno);
  LocateDirtySegment(GetSegmentNumber(old_blkaddr));
  LocateDirtySegment(GetSegmentNumber(new_blkaddr));
}

void SegmentManager::RewriteNodePage(Page *page, Summary *sum, block_t old_blkaddr,
                                     block_t new_blkaddr) {
  CursegType type = CursegType::kCursegWarmNode;
  CursegInfo *curseg;
  uint32_t segno, old_cursegno;
  block_t next_blkaddr = NodeManager::NextBlkaddrOfNode(*page);
  uint32_t next_segno = GetSegmentNumber(next_blkaddr);

  curseg = CURSEG_I(type);

#ifdef __Fuchsia__
  fbl::AutoLock curseg_lock(&curseg->curseg_mutex);
  fbl::AutoLock sentry_lock(&sit_info_->sentry_lock);
#endif  // __Fuchsia__

  segno = GetSegmentNumber(new_blkaddr);
  old_cursegno = curseg->segno;

  /* change the current segment */
  if (segno != curseg->segno) {
    curseg->next_segno = segno;
    ChangeCurseg(type, true);
  }
  curseg->next_blkoff = safemath::checked_cast<uint16_t>(GetSegOffFromSeg0(new_blkaddr) &
                                                         (superblock_info_->GetBlocksPerSeg() - 1));
  AddSumEntry(type, sum, curseg->next_blkoff);

  /* change the current log to the next block addr in advance */
  if (next_segno != segno) {
    curseg->next_segno = next_segno;
    ChangeCurseg(type, true);
  }
  curseg->next_blkoff = safemath::checked_cast<uint16_t>(GetSegOffFromSeg0(next_blkaddr) &
                                                         (superblock_info_->GetBlocksPerSeg() - 1));

  /* rewrite node page */
  SetPageWriteback(page);
  SubmitWritePage(page, new_blkaddr, PageType::kNode);
#if 0  // porting needed
  SubmitBio(NODE, true);
#endif
  RefreshSitEntry(old_blkaddr, new_blkaddr);

  LocateDirtySegment(old_cursegno);
  LocateDirtySegment(GetSegmentNumber(old_blkaddr));
  LocateDirtySegment(GetSegmentNumber(new_blkaddr));
}

int SegmentManager::ReadCompactedSummaries() {
  Checkpoint &ckpt = superblock_info_->GetCheckpoint();
  CursegInfo *seg_i;
  uint8_t *kaddr;
  Page *page = nullptr;
  block_t start;
  int i, j, offset;

  start = StartSumBlock();

  page = fs_->GetMetaPage(start++);
  kaddr = static_cast<uint8_t *>(PageAddress(page));

  // Step 1: restore nat cache
  seg_i = CURSEG_I(CursegType::kCursegHotData);
  memcpy(&seg_i->sum_blk->n_nats, kaddr, kSumJournalSize);

  // Step 2: restore sit cache
  seg_i = CURSEG_I(CursegType::kCursegColdData);
  memcpy(&seg_i->sum_blk->n_sits, kaddr + kSumJournalSize, kSumJournalSize);
  offset = 2 * kSumJournalSize;

  // Step 3: restore summary entries
  for (i = static_cast<int>(CursegType::kCursegHotData);
       i <= static_cast<int>(CursegType::kCursegColdData); ++i) {
    uint16_t blk_off;
    uint32_t segno;

    seg_i = CURSEG_I(static_cast<CursegType>(i));
    segno = LeToCpu(ckpt.cur_data_segno[i]);
    blk_off = LeToCpu(ckpt.cur_data_blkoff[i]);
    seg_i->next_segno = segno;
    ResetCurseg(static_cast<CursegType>(i), 0);
    seg_i->alloc_type = ckpt.alloc_type[i];
    seg_i->next_blkoff = blk_off;

    if (seg_i->alloc_type == static_cast<uint8_t>(AllocMode::kSSR))
      blk_off = static_cast<uint16_t>(superblock_info_->GetBlocksPerSeg());

    for (j = 0; j < blk_off; ++j) {
      Summary *s;
      s = reinterpret_cast<Summary *>(kaddr + offset);
      seg_i->sum_blk->entries[j] = *s;
      offset += kSummarySize;
      if (offset + kSummarySize <= kPageCacheSize - kSumFooterSize)
        continue;

      F2fsPutPage(page, 1);
      page = nullptr;

      page = fs_->GetMetaPage(start++);
      kaddr = static_cast<uint8_t *>(PageAddress(page));
      offset = 0;
    }
  }
  F2fsPutPage(page, 1);
  return 0;
}

int SegmentManager::ReadNormalSummaries(int type) {
  Checkpoint &ckpt = superblock_info_->GetCheckpoint();
  SummaryBlock *sum;
  CursegInfo *curseg;
  Page *new_page = nullptr;
  uint16_t blk_off;
  uint32_t segno = 0;
  block_t blk_addr = 0;

  // get segment number and block addr
  if (IsDataSeg(static_cast<CursegType>(type))) {
    segno = LeToCpu(ckpt.cur_data_segno[type]);
    blk_off = LeToCpu(ckpt.cur_data_blkoff[type - static_cast<int>(CursegType::kCursegHotData)]);
    if (ckpt.ckpt_flags & kCpUmountFlag) {
      blk_addr = SumBlkAddr(kNrCursegType, type);
    } else
      blk_addr = SumBlkAddr(kNrCursegDataType, type);
  } else {
    segno = LeToCpu(ckpt.cur_node_segno[type - static_cast<int>(CursegType::kCursegHotNode)]);
    blk_off = LeToCpu(ckpt.cur_node_blkoff[type - static_cast<int>(CursegType::kCursegHotNode)]);
    if (ckpt.ckpt_flags & kCpUmountFlag) {
      blk_addr = SumBlkAddr(kNrCursegNodeType, type - static_cast<int>(CursegType::kCursegHotNode));
    } else
      blk_addr = GetSumBlock(segno);
  }

  new_page = fs_->GetMetaPage(blk_addr);
  sum = static_cast<SummaryBlock *>(PageAddress(new_page));

  if (IsNodeSeg(static_cast<CursegType>(type))) {
    if (ckpt.ckpt_flags & kCpUmountFlag) {
      Summary *ns = &sum->entries[0];
      uint32_t i;
      for (i = 0; i < superblock_info_->GetBlocksPerSeg(); ++i, ++ns) {
        ns->version = 0;
        ns->ofs_in_node = 0;
      }
    } else {
      if (NodeManager::RestoreNodeSummary(*fs_, segno, *sum)) {
        F2fsPutPage(new_page, 1);
        return -EINVAL;
      }
    }
  }

  // set uncompleted segment to curseg
  curseg = CURSEG_I(static_cast<CursegType>(type));
  {
#ifdef __Fuchsia__
    fbl::AutoLock curseg_lock(&curseg->curseg_mutex);
#endif  // __Fuchsia__
    memcpy(curseg->sum_blk, sum, kPageCacheSize);
    curseg->next_segno = segno;
    ResetCurseg(static_cast<CursegType>(type), 0);
    curseg->alloc_type = ckpt.alloc_type[type];
    curseg->next_blkoff = blk_off;
  }
  F2fsPutPage(new_page, 1);
  return 0;
}

zx_status_t SegmentManager::RestoreCursegSummaries() {
  int type = static_cast<int>(CursegType::kCursegHotData);

  if (superblock_info_->GetCheckpoint().ckpt_flags & kCpCompactSumFlag) {
    // restore for compacted data summary
    if (ReadCompactedSummaries())
      return ZX_ERR_INVALID_ARGS;
    type = static_cast<int>(CursegType::kCursegHotNode);
  }

  for (; type <= static_cast<int>(CursegType::kCursegColdNode); ++type) {
    if (ReadNormalSummaries(type))
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

void SegmentManager::WriteCompactedSummaries(block_t blkaddr) {
  Page *page = nullptr;
  uint8_t *kaddr;
  Summary *summary;
  CursegInfo *seg_i;
  int written_size = 0;
  int i, j;

  page = fs_->GrabMetaPage(blkaddr++);
  kaddr = static_cast<uint8_t *>(PageAddress(page));

  // Step 1: write nat cache
  seg_i = CURSEG_I(CursegType::kCursegHotData);
  memcpy(kaddr, &seg_i->sum_blk->n_nats, kSumJournalSize);
  written_size += kSumJournalSize;

  // Step 2: write sit cache
  seg_i = CURSEG_I(CursegType::kCursegColdData);
  memcpy(kaddr + written_size, &seg_i->sum_blk->n_sits, kSumJournalSize);
  written_size += kSumJournalSize;

  // set_page_dirty(page);
  FlushDirtyMetaPage(fs_, page);

  // Step 3: write summary entries
  for (i = static_cast<int>(CursegType::kCursegHotData);
       i <= static_cast<int>(CursegType::kCursegColdData); ++i) {
    uint16_t blkoff;
    seg_i = CURSEG_I(static_cast<CursegType>(i));
    if (superblock_info_->GetCheckpoint().alloc_type[i] == static_cast<uint8_t>(AllocMode::kSSR)) {
      blkoff = static_cast<uint16_t>(superblock_info_->GetBlocksPerSeg());
    } else {
      blkoff = CursegBlkoff(i);
    }

    for (j = 0; j < blkoff; ++j) {
      if (!page) {
        page = fs_->GrabMetaPage(blkaddr++);
        kaddr = static_cast<uint8_t *>(PageAddress(page));
        written_size = 0;
      }
      summary = reinterpret_cast<Summary *>(kaddr + written_size);
      *summary = seg_i->sum_blk->entries[j];
      written_size += kSummarySize;
#if 0  // porting needed
      // set_page_dirty(page);
#endif
      FlushDirtyMetaPage(fs_, page);

      if (written_size + kSummarySize <= kPageCacheSize - kSumFooterSize)
        continue;

      F2fsPutPage(page, 1);
      page = nullptr;
    }
  }
  if (page)
    F2fsPutPage(page, 1);
}

void SegmentManager::WriteNormalSummaries(block_t blkaddr, CursegType type) {
  int i, end;

  if (IsDataSeg(type)) {
    end = static_cast<int>(type) + kNrCursegDataType;
  } else {
    end = static_cast<int>(type) + kNrCursegNodeType;
  }

  for (i = static_cast<int>(type); i < end; ++i) {
    CursegInfo *sum = CURSEG_I(static_cast<CursegType>(i));
#ifdef __Fuchsia__
    fbl::AutoLock curseg_lock(&sum->curseg_mutex);
#endif  // __Fuchsia__
    WriteSumPage(sum->sum_blk, blkaddr + (i - static_cast<int>(type)));
  }
}

void SegmentManager::WriteDataSummaries(block_t start_blk) {
  if (superblock_info_->GetCheckpoint().ckpt_flags & kCpCompactSumFlag) {
    WriteCompactedSummaries(start_blk);
  } else {
    WriteNormalSummaries(start_blk, CursegType::kCursegHotData);
  }
}

void SegmentManager::WriteNodeSummaries(block_t start_blk) {
  if (superblock_info_->GetCheckpoint().ckpt_flags & kCpUmountFlag)
    WriteNormalSummaries(start_blk, CursegType::kCursegHotNode);
}

int LookupJournalInCursum(SummaryBlock *sum, JournalType type, uint32_t val, int alloc) {
  int i;

  if (type == JournalType::kNatJournal) {
    for (i = 0; i < NatsInCursum(sum); ++i) {
      if (LeToCpu(NidInJournal(sum, i)) == val)
        return i;
    }
    if (alloc && NatsInCursum(sum) < static_cast<int>(kNatJournalEntries))
      return UpdateNatsInCursum(sum, 1);
  } else if (type == JournalType::kSitJournal) {
    for (i = 0; i < SitsInCursum(sum); ++i) {
      if (LeToCpu(SegnoInJournal(sum, i)) == val)
        return i;
    }
    if (alloc && SitsInCursum(sum) < static_cast<int>(kSitJournalEntries))
      return UpdateSitsInCursum(sum, 1);
  }
  return -1;
}

Page *SegmentManager::GetCurrentSitPage(uint32_t segno) {
  uint32_t offset = SitBlockOffset(segno);
  block_t blk_addr = sit_info_->sit_base_addr + offset;

  CheckSegRange(segno);

  // calculate sit block address
  if (TestValidBitmap(offset, sit_info_->sit_bitmap.get()))
    blk_addr += sit_info_->sit_blocks;

  return fs_->GetMetaPage(blk_addr);
}

Page *SegmentManager::GetNextSitPage(uint32_t start) {
  Page *src_page = nullptr, *dst_page = nullptr;
  pgoff_t src_off, dst_off;
  void *src_addr, *dst_addr;

  src_off = CurrentSitAddr(start);
  dst_off = NextSitAddr(src_off);

  // get current sit block page without lock
  src_page = fs_->GetMetaPage(src_off);
  dst_page = fs_->GrabMetaPage(dst_off);
  ZX_ASSERT(!PageDirty(src_page));

  src_addr = PageAddress(src_page);
  dst_addr = PageAddress(dst_page);
  memcpy(dst_addr, src_addr, kPageCacheSize);

#if 0  // porting needed
  // set_page_dirty(dst_page);
#endif
  F2fsPutPage(src_page, 1);

  SetToNextSit(start);

  return dst_page;
}

bool SegmentManager::FlushSitsInJournal() {
  CursegInfo *curseg = CURSEG_I(CursegType::kCursegColdData);
  SummaryBlock *sum = curseg->sum_blk;
  int i;

  // If the journal area in the current summary is full of sit entries,
  // all the sit entries will be flushed. Otherwise the sit entries
  // are not able to replace with newly hot sit entries.
  if ((SitsInCursum(sum) + sit_info_->dirty_sentries) > static_cast<int>(kSitJournalEntries)) {
    for (i = SitsInCursum(sum) - 1; i >= 0; --i) {
      uint32_t segno;
      segno = LeToCpu(SegnoInJournal(sum, i));
      MarkSitEntryDirty(segno);
    }
    UpdateSitsInCursum(sum, -SitsInCursum(sum));
    return true;
  }
  return false;
}

// CP calls this function, which flushes SIT entries including SitJournal,
// and moves prefree segs to free segs.
void SegmentManager::FlushSitEntries() {
  uint8_t *bitmap = sit_info_->dirty_sentries_bitmap.get();
  CursegInfo *curseg = CURSEG_I(CursegType::kCursegColdData);
  SummaryBlock *sum = curseg->sum_blk;
  block_t nsegs = TotalSegs();
  Page *page = nullptr;
  SitBlock *raw_sit = nullptr;
  uint32_t start = 0, end = 0;
  uint32_t segno = -1;
  bool flushed;

  {
#ifdef __Fuchsia__
    fbl::AutoLock curseg_lock(&curseg->curseg_mutex);
    fbl::AutoLock sentry_lock(&sit_info_->sentry_lock);
#endif  // __Fuchsia__

    // "flushed" indicates whether sit entries in journal are flushed
    // to the SIT area or not.
    flushed = FlushSitsInJournal();

    while ((segno = FindNextBit(bitmap, nsegs, segno + 1)) < nsegs) {
      SegmentEntry &segment_entry = GetSegmentEntry(segno);
      int sit_offset, offset = -1;

      sit_offset = SitEntryOffset(segno);

      if (!flushed) {
        offset = LookupJournalInCursum(sum, JournalType::kSitJournal, segno, 1);
      }

      if (offset >= 0) {
        SetSegnoInJournal(sum, offset, CpuToLe(segno));
        SegInfoToRawSit(segment_entry, SitInJournal(sum, offset));
      } else {
        if (!page || (start > segno) || (segno > end)) {
          if (page) {
            // set_page_dirty(page, fs_);
            FlushDirtyMetaPage(fs_, page);
            F2fsPutPage(page, 1);
            page = nullptr;
          }

          start = StartSegNo(segno);
          end = start + kSitEntryPerBlock - 1;

          // read sit block that will be updated
          page = GetNextSitPage(start);
          raw_sit = static_cast<SitBlock *>(PageAddress(page));
        }

        // udpate entry in SIT block
        SegInfoToRawSit(segment_entry, raw_sit->entries[sit_offset]);
      }
      ClearBit(segno, bitmap);
      --sit_info_->dirty_sentries;
    }
  }
  // writeout last modified SIT block
#if 0  // porting needed
  // set_page_dirty(page, fs_);
#endif
  FlushDirtyMetaPage(fs_, page);
  F2fsPutPage(page, 1);

  SetPrefreeAsFreeSegments();
}

zx_status_t SegmentManager::BuildSitInfo() {
  const Superblock &raw_super = superblock_info_->GetRawSuperblock();
  Checkpoint &ckpt = superblock_info_->GetCheckpoint();
  uint32_t sit_segs, start;
  uint8_t *src_bitmap;
  uint32_t bitmap_size;

  // allocate memory for SIT information
  sit_info_ = std::make_unique<SitInfo>();

  SitInfo *sit_i = sit_info_.get();
  if (sit_i->sentries = new SegmentEntry[TotalSegs()]; !sit_i->sentries) {
    return ZX_ERR_NO_MEMORY;
  }

  bitmap_size = BitmapSize(TotalSegs());
  sit_i->dirty_sentries_bitmap = std::make_unique<uint8_t[]>(bitmap_size);

  for (start = 0; start < TotalSegs(); ++start) {
    sit_i->sentries[start].cur_valid_map = std::make_unique<uint8_t[]>(kSitVBlockMapSize);
    sit_i->sentries[start].ckpt_valid_map = std::make_unique<uint8_t[]>(kSitVBlockMapSize);
  }

  if (superblock_info_->GetSegsPerSec() > 1) {
    if (sit_i->sec_entries = new SectionEntry[superblock_info_->GetTotalSections()];
        !sit_i->sec_entries) {
      return ZX_ERR_NO_MEMORY;
    }
  }

  // get information related with SIT
  sit_segs = LeToCpu(raw_super.segment_count_sit) >> 1;

  // setup SIT bitmap from ckeckpoint pack
  bitmap_size = superblock_info_->BitmapSize(MetaBitmap::kSitBitmap);
  src_bitmap = static_cast<uint8_t *>(superblock_info_->BitmapPtr(MetaBitmap::kSitBitmap));

  sit_i->sit_bitmap = std::make_unique<uint8_t[]>(bitmap_size);
  memcpy(sit_i->sit_bitmap.get(), src_bitmap, bitmap_size);

#if 0  // porting needed
  /* init SIT information */
  // sit_i->s_ops = &default_salloc_ops;
#endif
  auto cur_time = time(nullptr);

  sit_i->sit_base_addr = LeToCpu(raw_super.sit_blkaddr);
  sit_i->sit_blocks = sit_segs << superblock_info_->GetLogBlocksPerSeg();
  sit_i->written_valid_blocks = LeToCpu(static_cast<block_t>(ckpt.valid_block_count));
  sit_i->bitmap_size = bitmap_size;
  sit_i->dirty_sentries = 0;
  sit_i->sents_per_block = kSitEntryPerBlock;
  sit_i->elapsed_time = LeToCpu(superblock_info_->GetCheckpoint().elapsed_time);
  sit_i->mounted_time = cur_time;
  return ZX_OK;
}

zx_status_t SegmentManager::BuildFreeSegmap() {
  uint32_t bitmap_size, sec_bitmap_size;

  // allocate memory for free segmap information
  free_info_ = std::make_unique<FreeSegmapInfo>();

  bitmap_size = BitmapSize(TotalSegs());
  free_info_->free_segmap = std::make_unique<uint8_t[]>(bitmap_size);

  sec_bitmap_size = BitmapSize(superblock_info_->GetTotalSections());
  free_info_->free_secmap = std::make_unique<uint8_t[]>(sec_bitmap_size);

  // set all segments as dirty temporarily
  memset(free_info_->free_segmap.get(), 0xff, bitmap_size);
  memset(free_info_->free_secmap.get(), 0xff, sec_bitmap_size);

  // init free segmap information
  free_info_->start_segno = GetSegNoFromSeg0(main_blkaddr_);
  free_info_->free_segments = 0;
  free_info_->free_sections = 0;

  return ZX_OK;
}

zx_status_t SegmentManager::BuildCurseg() {
  for (int i = 0; i < kNrCursegType; ++i) {
    if (curseg_array_[i].raw_blk = new FsBlock(); !curseg_array_[i].raw_blk) {
      return ZX_ERR_NO_MEMORY;
    }
    curseg_array_[i].segno = kNullSegNo;
    curseg_array_[i].next_blkoff = 0;
  }
  return RestoreCursegSummaries();
}

void SegmentManager::BuildSitEntries() {
  CursegInfo *curseg = CURSEG_I(CursegType::kCursegColdData);
  SummaryBlock *sum = curseg->sum_blk;

  for (uint32_t start = 0; start < TotalSegs(); ++start) {
    SegmentEntry &segment_entry = sit_info_->sentries[start];
    SitBlock *sit_blk;
    SitEntry sit;
    Page *page = nullptr;
    bool got_it = false;
    {
#ifdef __Fuchsia__
      fbl::AutoLock curseg_lock(&curseg->curseg_mutex);
#endif  // __Fuchsia__
      for (int i = 0; i < SitsInCursum(sum); ++i) {
        if (LeToCpu(SegnoInJournal(sum, i)) == start) {
          sit = SitInJournal(sum, i);
          got_it = true;
          break;
        }
      }
    }
    if (!got_it) {
      page = GetCurrentSitPage(start);
      sit_blk = static_cast<SitBlock *>(PageAddress(page));
      sit = sit_blk->entries[SitEntryOffset(start)];
      F2fsPutPage(page, 1);
    }
    CheckBlockCount(start, sit);
    SegInfoFromRawSit(segment_entry, sit);
    if (superblock_info_->GetSegsPerSec() > 1) {
      SectionEntry *e = GetSectionEntry(start);
      e->valid_blocks += segment_entry.valid_blocks;
    }
  }
}

void SegmentManager::InitFreeSegmap() {
  uint32_t start;
  int type;

  for (start = 0; start < TotalSegs(); ++start) {
    SegmentEntry &sentry = GetSegmentEntry(start);
    if (!sentry.valid_blocks)
      SetFree(start);
  }

  // set use the current segments
  for (type = static_cast<int>(CursegType::kCursegHotData);
       type <= static_cast<int>(CursegType::kCursegColdNode); ++type) {
    CursegInfo *curseg_t = CURSEG_I(static_cast<CursegType>(type));
    SetTestAndInuse(curseg_t->segno);
  }
}

void SegmentManager::InitDirtySegmap() {
  uint32_t segno = 0, offset = 0;
  uint16_t valid_blocks;
  int full_block_cnt = 0, dirty_block_cnt = 0;

  while (segno < TotalSegs()) {
    /* find dirty segment based on free segmap */
    segno = FindNextInuse(TotalSegs(), offset);
    if (segno >= TotalSegs())
      break;
    offset = segno + 1;
    valid_blocks = static_cast<uint16_t>(GetValidBlocks(segno, 0));
    if (valid_blocks >= superblock_info_->GetBlocksPerSeg() || !valid_blocks) {
      ++full_block_cnt;
      continue;
    }
#ifdef __Fuchsia__
    fbl::AutoLock seglist_lock(&dirty_info_->seglist_lock);
#endif  // __Fuchsia__
    LocateDirtySegment(segno, DirtyType::kDirty);
    ++dirty_block_cnt;
  }
}

zx_status_t SegmentManager::InitVictimSegmap() {
  uint32_t bitmap_size = BitmapSize(TotalSegs());

  dirty_info_->victim_segmap[static_cast<int>(GcType::kFgGc)] =
      std::make_unique<uint8_t[]>(bitmap_size);
  dirty_info_->victim_segmap[static_cast<int>(GcType::kBgGc)] =
      std::make_unique<uint8_t[]>(bitmap_size);
  return ZX_OK;
}

zx_status_t SegmentManager::BuildDirtySegmap() {
  uint32_t bitmap_size, i;

  dirty_info_ = std::make_unique<DirtySeglistInfo>();
  bitmap_size = BitmapSize(TotalSegs());

  for (i = 0; i < static_cast<int>(DirtyType::kNrDirtytype); ++i) {
    dirty_info_->dirty_segmap[i] = std::make_unique<uint8_t[]>(bitmap_size);
    dirty_info_->nr_dirty[i] = 0;
  }

  InitDirtySegmap();
  return InitVictimSegmap();
}

// Update min, max modified time for cost-benefit GC algorithm
void SegmentManager::InitMinMaxMtime() {
  uint32_t segno;

#ifdef __Fuchsia__
  fbl::AutoLock sentry_lock(&sit_info_->sentry_lock);
#endif  // __Fuchsia__

  sit_info_->min_mtime = LLONG_MAX;

  for (segno = 0; segno < TotalSegs(); segno += superblock_info_->GetSegsPerSec()) {
    uint32_t i;
    uint64_t mtime = 0;

    for (i = 0; i < superblock_info_->GetSegsPerSec(); ++i) {
      mtime += GetSegmentEntry(segno + i).mtime;
    }

    mtime /= static_cast<uint64_t>(superblock_info_->GetSegsPerSec());

    if (sit_info_->min_mtime > mtime) {
      sit_info_->min_mtime = mtime;
    }
  }
  sit_info_->max_mtime = GetMtime();
}

zx_status_t SegmentManager::BuildSegmentManager() {
  const Superblock &raw_super = superblock_info_->GetRawSuperblock();
  Checkpoint &ckpt = superblock_info_->GetCheckpoint();
  zx_status_t err = 0;

  seg0_blkaddr_ = LeToCpu(raw_super.segment0_blkaddr);
  main_blkaddr_ = LeToCpu(raw_super.main_blkaddr);
  segment_count_ = LeToCpu(raw_super.segment_count);
  reserved_segments_ = LeToCpu(ckpt.rsvd_segment_count);
  ovp_segments_ = LeToCpu(ckpt.overprov_segment_count);
  main_segments_ = LeToCpu(raw_super.segment_count_main);
  ssa_blkaddr_ = LeToCpu(raw_super.ssa_blkaddr);

  err = BuildSitInfo();
  if (err)
    return err;

  err = BuildFreeSegmap();
  if (err)
    return err;

  err = BuildCurseg();
  if (err)
    return err;

  // reinit free segmap based on SIT
  BuildSitEntries();

  InitFreeSegmap();
  err = BuildDirtySegmap();
  if (err)
    return err;

  InitMinMaxMtime();
  return ZX_OK;
}

void SegmentManager::DiscardDirtySegmap(DirtyType dirty_type) {
#ifdef __Fuchsia__
  fbl::AutoLock seglist_lock(&dirty_info_->seglist_lock);
#endif  // __Fuchsia__
  dirty_info_->dirty_segmap[static_cast<int>(dirty_type)].reset();
  dirty_info_->nr_dirty[static_cast<int>(dirty_type)] = 0;
}

void SegmentManager::ResetVictimSegmap() {
  uint32_t bitmap_size = BitmapSize(TotalSegs());
  memset(dirty_info_->victim_segmap[static_cast<int>(GcType::kFgGc)].get(), 0, bitmap_size);
}

void SegmentManager::DestroyVictimSegmap() {
  dirty_info_->victim_segmap[static_cast<int>(GcType::kFgGc)].reset();
  dirty_info_->victim_segmap[static_cast<int>(GcType::kBgGc)].reset();
}

void SegmentManager::DestroyDirtySegmap() {
  if (!dirty_info_)
    return;

  /* discard pre-free/dirty segments list */
  for (int i = 0; i < static_cast<int>(DirtyType::kNrDirtytype); ++i) {
    DiscardDirtySegmap(static_cast<DirtyType>(i));
  }

  DestroyVictimSegmap();
  dirty_info_.reset();
}

// TODO: destroy_curseg
void SegmentManager::DestroyCurseg() {
  for (int i = 0; i < kNrCursegType; ++i)
    delete curseg_array_[i].raw_blk;
}

void SegmentManager::DestroyFreeSegmap() {
  if (!free_info_)
    return;
  free_info_->free_segmap.reset();
  free_info_->free_secmap.reset();
  free_info_.reset();
}

void SegmentManager::DestroySitInfo() {
  uint32_t start;

  if (!sit_info_)
    return;

  if (sit_info_->sentries) {
    for (start = 0; start < TotalSegs(); ++start) {
      sit_info_->sentries[start].cur_valid_map.reset();
      sit_info_->sentries[start].ckpt_valid_map.reset();
    }
  }
  delete[] sit_info_->sentries;
  delete[] sit_info_->sec_entries;
  sit_info_->dirty_sentries_bitmap.reset();
  sit_info_->sit_bitmap.reset();
}

void SegmentManager::DestroySegmentManager() {
  DestroyDirtySegmap();
  DestroyCurseg();
  DestroyFreeSegmap();
  DestroySitInfo();
}

}  // namespace f2fs

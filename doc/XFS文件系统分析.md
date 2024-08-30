# XFS文件系统分析

### 核心流程

#### Commit Transaction

#### Transaction Persist

#### cil to ail

#### ali persistence

### XFS命令使用

#### xfs_info

#### xfs_metadump

#### xfs_db

#### xfs_logprint

xfs_logprint -o /dev/loop0

#### xxd

### 分析命令

##### 查看函数调用堆栈

```
trace-cmd start -p function -l xlog_bio_end_io --func-stack
trace-cmd start -p function -l xlog_write_iclog --func-stack
trace-cmd start -p function -l xfs_fs_sync_fs --func-stack
trace-cmd start -p function -l xfs_log_work_queue --func-stack
trace-cmd start -p function -l xfs_trans_commit --func-stack
trace-cmd start -p function -l 'xlog_cil_push*' --func-stack
trace-cmd start -p function -l xlog_cil_push_now --func-stack
trace-cmd start -p function -l xlog_cil_push_background --func-stack
trace-cmd start -p function -l 'xfs_alloc_*' -l 'xfs_bmap*' --func-stack
trace-cmd start -p function -l xlog_write_iclog --func-stack
```

start命令后接上trace-cmd show看输出结果，还可以使用perf-tools工具集里的kprobe，关闭就使用命令trace-cmd stop

```
kprobe -s -H p:xfs_iflush_cluster
kprobe -s -H p:__xfs_log_force_lsn
kprobe -s -H p:xfs_rw_bdev
kprobe -s -H p:xlog_write_iclog
kprobe -s -H p:xlog_cil_push_work
kprobe -s -H p:xlog_sync
kprobe 'r:__xlog_state_release_iclog $retval'
kprobe 'p:xlog_state_release_iclog '
kprobe -s -H p:xfs_agf_write_verify
kprobe -s -H 'r:xfs_buf_get_map $retval'
kprobe -s -H p:xlog_write_iclog
kprobe -s -H p:xfs_trans_add_item 查看事务添加日志项
kprobe -s -H p:xfs_bmap_add_extent_hole_real
```

##### 查看函数子调用

```
trace-cmd record -p function_graph -g xfs_file_fallocate
trace-cmd record -p function_graph -g xfs_trans_commit
trace-cmd record -p function_graph -g __xfs_log_force_lsn
trace-cmd record -p function_graph -g xlog_cil_push_work 
trace-cmd record -p function_graph  -P 265793 -P 264849 --max-graph-depth 15
trace-cmd record -p function_graph -g xfs_log_commit_cil  --max-graph-depth 2 
trace-cmd record -p function_graph -g xfs_buf_read_map
trace-cmd record -p function_graph -g xfs_bmap_btalloc
trace-cmd record -p function_graph -g xlog_cil_push_work
trace-cmd record -p function_graph -g xfs_alloc_file_space
trace-cmd record -p function_graph -g xfs_bmapi_allocate
trace-cmd record -p function_graph -g xfs_bmap_add_extent_hole_real --max-graph-depth 4
```

record之后使用trace-cmd report -L -I -S > report.txt来查看子调用过程

##### 跟踪xfs的tracepoint

查看内核支持的tracepoint：perf list event

```
perf trace -a -e 'xfs_log_force*' --kernel-syscall-graph --comm --failure --call-graph dwarf --max-event 4
perf trace -a -e 'xfs:xfs_log_force*' --kernel-syscall-graph --comm --failure --call-graph dwarf --max-event 4
perf trace -a -e 'xfs:xlog_cil_push_work' --kernel-syscall-graph --comm --failure --call-graph dwarf 
perf trace -e 'xfs:xfs_ail_move' -a --kernel-syscall-graph --comm --failure --call-graph dwarf/fp
perf trace -e 'xfs:xfs_ail_mo*-a --kernel-syscall-graph --comm --failure --call-graph fp
perf trace -e 'xfs:xfs_ail_*' -a --kernel-syscall-graph --comm --failure --call-graph fp
```

##### 查看XFS某个函数的代码，以及可见的变量

```
perf probe -L xfs_log_commit_cil -m /usr/lib/debug/lib/modules/4.18.0-348.7.1.el8_5.x86_64+debug/kernel/fs/xfs/xfs.ko.debug
```

```
perf probe -L xlog_assign_tail_lsn_locked -m /usr/lib/debug/lib/modules/4.18.0-425.3.1.el8.x86_64+debug/kernel/fs/xfs/xfs.ko.debug

<xlog_assign_tail_lsn_locked@/usr/src/debug/kernel-4.18.0-425.3.1.el8/linux-4.18.0-425.3.1.el8.x86_64/fs/xfs/xfs_log.c:0>
      0  xlog_assign_tail_lsn_locked(
                struct xfs_mount        *mp)
         {
      3         struct xlog             *log = mp->m_log;
      4         struct xfs_log_item     *lip;
                xfs_lsn_t               tail_lsn;
         
      7         assert_spin_locked(&mp->m_ail->ail_lock);
         
                /*
                 * To make sure we always have a valid LSN for the log tail we keep
                 * track of the last LSN which was committed in log->l_last_sync_lsn,
                 * and use that when the AIL was empty.
                 */
                lip = xfs_ail_min(mp->m_ail);
                if (lip)
     16                 tail_lsn = lip->li_lsn;
                else
     18                 tail_lsn = atomic64_read(&log->l_last_sync_lsn);
     19         trace_xfs_log_assign_tail_lsn(log, tail_lsn);
     20         atomic64_set(&log->l_tail_lsn, tail_lsn);
                return tail_lsn;
         }
```

##### 查看内核源码是什么安装包安装的

```
rpm -qf /usr/src/debug/kernel-4.18.0-425.3.1.el8/linux-4.18.0-425.3.1.el8.x86_64/fs/xfs/xfs_log.c
kernel-debuginfo-common-x86_64-4.18.0-425.3.1.el8.x86_64
```

##### 使用SystemTap查看函数声明

```
stap -L 'kernel.function("vfs_read")'
stap -L 'kernel.function("vfs_write")'
stap -L 'module("xfs").function("xfs_agf_write_verify")'
stap -L 'module("xfs").function("xlog_cil_push_background")'
stap -L 'module("xfs").function("xfs_bmapi_write").return'
```

##### 定位函数代码行

```
⚡ root@localhost  /home/calmwu/Program/x-monitor/tools/bin  ./faddr2line /usr/lib/debug/lib/modules/4.18.0-425.19.2.el8_7.x86_64/kernel/fs/xfs/xfs.ko.debug xfs_buf_ioend+0x75
xfs_buf_ioend+0x75/0x620:
xfs_buf_ioend at /usr/src/debug/kernel-4.18.0-425.19.2.el8_7/linux-4.18.0-425.19.2.el8_7.x86_64/fs/xfs/xfs_buf.c:1355
```

也可以用addr2line，就是要找到模块的基地址，然后转换

```
Fri May 31 06:30:00 2024, tid:171934, agf:{dev:[7:0], agno:0}, xfs_buf_item_relse execname:kworker/4:3
 0xffffffffc0519320 : xfs_buf_item_relse+0x0/0x80 [xfs]
 0xffffffffc04ece85 : xfs_buf_ioend+0x75/0x620 [xfs]
 
 ⚡ root@localhost  /home/calmwu/Program/x-monitor/tools/bin  cat /proc/modules|grep xfs
xfs 1568768 4 - Live 0xffffffffc048a000
```

c04ece85 -  c048a000 = 0x62E85

```
 ⚡ root@localhost  /home/calmwu/Program/x-monitor/tools/bin  addr2line -e /usr/lib/debug/lib/modules/4.18.0-425.19.2.el8_7.x86_64/kernel/fs/xfs/xfs.ko.debug 0x62E85
/usr/src/debug/kernel-4.18.0-425.19.2.el8_7/linux-4.18.0-425.19.2.el8_7.x86_64/fs/xfs/xfs_buf.c:1355
```

由于麒麟没有提供kernel-debuginfo-common-`uname -r`rpm包，所以没法在gdb中直接使用disassemble和list命令对应到代码

```
(gdb) list *(xfs_bmap_extents_to_btree+0x533)
0x359d3 is in xfs_bmap_extents_to_btree (fs/xfs/libxfs/xfs_bmap.c:657).
652	in fs/xfs/libxfs/xfs_bmap.c
```

#### 麒麟xfs shutdown堆栈分析

堆栈顶部，RIP指令

```
xfs_bmap_extents_to_btree+0x533
```

使用gdb查看xfs.ko，+1331偏移处的指令

```
disassemble xfs_bmap_extents_to_btree
0x000000000001a933 <+1331>:	ud2
```

ud2:用于生成一个未定义的指令异常（Invalid Opcode Exception）

查看从哪里跳转到此处

```
   0x000000000001a56f <+367>:	cmp    $0xffffffffffffffff,%rax
   0x000000000001a573 <+371>:	je     0x1a933 <xfs_bmap_extents_to_btree+1331>
```

这里cmp指令，结合代码就是

```
	if (WARN_ON_ONCE(args.fsbno == NULLFSBLOCK)) {
		error = -ENOSPC;
		goto out_root_realloc;
	}
```

NULLFSBLOCK = -1

说明前面args.fsbo被设置为NULLFSBLOCK

```
	if (tp->t_firstblock == NULLFSBLOCK) {
		args.type = XFS_ALLOCTYPE_START_BNO;
		args.fsbno = XFS_INO_TO_FSB(mp, ip->i_ino);
	} else if (tp->t_flags & XFS_TRANS_LOWMODE) {
		args.type = XFS_ALLOCTYPE_START_BNO;
		args.fsbno = tp->t_firstblock;
	} else {
		args.type = XFS_ALLOCTYPE_NEAR_BNO;
		args.fsbno = tp->t_firstblock;
	}
```

使用systemtap去观察**xfs_alloc_vexten**t函数，看args的fsbno，同时观察函数**xfs_bmap_extents_to_btree**函数参数tp的t_firstblock和t_flags，获取fsbno取值逻辑。

### 函数与数据结构

#### fallocate分配空间，修改ag的过程

是 XFS 文件系统中的一个关键函数，用于从自由空间中分配一个新的文件系统块范围（extent）,这个过程涉及读取和更新AG元数据，包括AGF和AGFL

**读取AGF和AGFL**：

- `xfs_alloc_read_agf`：读取 AGF（Allocation Group Free space information）的内容，并锁住相关的缓冲区，以防止其他进程修改。

- `xfs_alloc_read_agfl`：读取 AGFL（Allocation Group Free List）的内容，并锁住相关的缓冲区。
- 这些函数将 AGF 和 AGFL 的内容读入内存，并创建相应的 `xfs_buf_log_item`（用于日志记录）。

```
       fallocate-10567 [004]  8806.761988: funcgraph_entry:                   |  xfs_alloc_vextent() {
       fallocate-10567 [004]  8806.764130: funcgraph_entry:      + 39.300 us  |    xfs_perag_get();
       fallocate-10567 [004]  8806.764286: funcgraph_entry:                   |    xfs_alloc_fix_freelist() {
       fallocate-10567 [004]  8806.764365: funcgraph_entry:      + 39.000 us  |      xfs_alloc_min_freelist();
       fallocate-10567 [004]  8806.764522: funcgraph_entry:                   |      xfs_alloc_space_available() {
       fallocate-10567 [004]  8806.764603: funcgraph_entry:      + 39.700 us  |        xfs_ag_resv_needed();
       fallocate-10567 [004]  8806.764761: funcgraph_entry:      + 38.800 us  |        xfs_alloc_longest_free_extent();
       fallocate-10567 [004]  8806.764953: funcgraph_exit:       ! 352.700 us |      }
       fallocate-10567 [004]  8806.764993: funcgraph_entry:                   |      xfs_alloc_read_agf() {  ----》读取agf信息，保存在xfs_buf中
       fallocate-10567 [004]  8806.765071: funcgraph_entry:      + 43.300 us  |        xfs_read_agf();
       fallocate-10567 [004]  8806.765269: funcgraph_exit:       ! 199.600 us |      }
       fallocate-10567 [004]  8806.765309: funcgraph_entry:      + 39.200 us  |      xfs_alloc_min_freelist();
       fallocate-10567 [004]  8806.765464: funcgraph_entry:                   |      xfs_alloc_space_available() {
       fallocate-10567 [004]  8806.765542: funcgraph_entry:      + 39.000 us  |        xfs_ag_resv_needed();
       fallocate-10567 [004]  8806.765697: funcgraph_entry:      + 39.100 us  |        xfs_alloc_longest_free_extent();
       fallocate-10567 [004]  8806.765891: funcgraph_exit:       ! 350.500 us |      }
       fallocate-10567 [004]  8806.765932: funcgraph_entry:                   |      xfs_alloc_read_agfl() {
       fallocate-10567 [004]  8806.766013: funcgraph_entry:      + 46.600 us  |        xfs_trans_read_buf_map();
       fallocate-10567 [004]  8806.766179: funcgraph_entry:      + 38.700 us  |        xfs_buf_set_ref();
       fallocate-10567 [004]  8806.766371: funcgraph_exit:       ! 360.800 us |      }
       fallocate-10567 [004]  8806.766412: funcgraph_entry:                   |      xfs_trans_brelse() {
       fallocate-10567 [004]  8806.766491: funcgraph_entry:      + 39.000 us  |        xfs_trans_del_item();
       fallocate-10567 [004]  8806.766647: funcgraph_entry:      + 42.100 us  |        xfs_buf_item_put();
       fallocate-10567 [004]  8806.766806: funcgraph_entry:      + 40.100 us  |        xfs_buf_unlock();
       fallocate-10567 [004]  8806.766964: funcgraph_entry:      + 40.100 us  |        xfs_buf_rele();
       fallocate-10567 [004]  8806.767158: funcgraph_exit:       ! 669.300 us |      }
       fallocate-10567 [004]  8806.767237: funcgraph_exit:       # 2873.500 us |    }
       fallocate-10567 [004]  8806.767277: funcgraph_entry:                   |    xfs_alloc_ag_vextent() {
       fallocate-10567 [004]  8806.774668: funcgraph_entry:                   |      xfs_alloc_ag_vextent_size() {
       fallocate-10567 [004]  8806.774747: funcgraph_entry:                   |        xfs_allocbt_init_cursor()
       fallocate-10567 [004]  8806.775252: funcgraph_entry:      + 38.100 us  |        xfs_btree_lookup();
       fallocate-10567 [004]  8806.775553: funcgraph_entry:                   |        xfs_alloc_get_rec()
       fallocate-10567 [004]  8806.776167: funcgraph_entry:      + 18.300 us  |        xfs_alloc_compute_aligned();
       fallocate-10567 [004]  8806.776236: funcgraph_entry:      + 16.700 us  |        xfs_alloc_fix_len();
       fallocate-10567 [004]  8806.776305: funcgraph_entry:      + 19.700 us  |        xfs_allocbt_init_cursor();
       fallocate-10567 [004]  8806.776376: funcgraph_entry:      + 38.300 us  |        xfs_alloc_fixup_trees();
       fallocate-10567 [004]  8806.776466: funcgraph_entry:      + 18.200 us  |        xfs_btree_del_cursor();
       fallocate-10567 [004]  8806.776535: funcgraph_entry:      + 18.100 us  |        xfs_btree_del_cursor();
       fallocate-10567 [004]  8806.776620: funcgraph_exit:       # 1896.100 us |      }
       fallocate-10567 [004]  8806.776638: funcgraph_entry:                   |      xfs_alloc_update_counters() { ----》更新agf元数据，内存中
       fallocate-10567 [004]  8806.776675: funcgraph_entry:      + 17.500 us  |        xfs_alloc_log_agf(); ---》记录修改的日志，xfs_log_item设置为dirty
       fallocate-10567 [004]  8806.776760: funcgraph_exit:       + 85.500 us  |      }
       fallocate-10567 [004]  8806.776778: funcgraph_entry:                   |      xfs_ag_resv_alloc_extent() {
       fallocate-10567 [004]  8806.776814: funcgraph_entry:      + 17.200 us  |        xfs_trans_mod_sb(); ----》修改事务中心记录的超级块信息
       fallocate-10567 [004]  8806.776905: funcgraph_entry:      ! 392.000 us |        smp_apic_timer_interrupt();
       fallocate-10567 [004]  8806.777348: funcgraph_exit:       + 87.400 us  |      }
       fallocate-10567 [004]  8806.777382: funcgraph_exit:       # 2739.700 us |    }
       fallocate-10567 [004]  8806.777400: funcgraph_entry:      + 16.699 us  |    xfs_perag_put();
       fallocate-10567 [004]  8806.777484: funcgraph_exit:       # 15437.300 us |  }
```

#### 读取agf元数据信息加入事务的日志项列表中（修改前）

当xfs的元数据发生变化时，会记录在xfs_log_item中，例如xfs_buf_log_item就绑定一个xfs_buf，xfs_buf就记录了磁盘上某个元数据结构，例如xfs_alloc_read_agfl、xfs_alloc_read_agf

```
       fallocate-96951 [007] .... 84936.458528: xfs_trans_add_item: (xfs_trans_add_item+0x0/0xb0 [xfs])
       fallocate-96951 [007] .... 84936.458587: <stack trace>
 => xfs_trans_add_item
 => _xfs_trans_bjoin ----》在这里将读取的agf对应的xfs_buf初始化其b_log_item(xfs_buf_log_item)，这里是当前没有修改的数据，生成日志项，加入事务中
 => xfs_trans_read_buf_map
 => xfs_read_agf
 => xfs_alloc_read_agf
     => xfs_alloc_fix_freelist
 => xfs_alloc_vextent
 => xfs_bmap_btalloc
 => xfs_bmapi_allocate
 => xfs_bmapi_write
 => xfs_alloc_file_space ----》在这里构造xfs_tran
 => xfs_file_fallocate
 => vfs_fallocate
 => ksys_fallocate
 => __x64_sys_fallocate
 => do_syscall_64
```

将xfs_log_item链表结构li_trans

```
struct xfs_log_item {
	struct list_head li_ail; /* AIL pointers */
	struct list_head li_trans; /* transaction list */
```

加入到事务的t_items链表中

```
typedef struct xfs_trans {
	struct list_head t_items; /* log item descriptors */
```

```
xfs_trans_add_item(
	struct xfs_trans	*tp,
	struct xfs_log_item	*lip)
{
	ASSERT(lip->li_mountp == tp->t_mountp);
	ASSERT(lip->li_ailp == tp->t_mountp->m_ail);
	ASSERT(list_empty(&lip->li_trans));
	ASSERT(!test_bit(XFS_LI_DIRTY, &lip->li_flags));

	list_add_tail(&lip->li_trans, &tp->t_items);
	trace_xfs_trans_add_item(tp, _RET_IP_);
}
```

在_xfs_trans_bjoin函数中给xfs_buf分配xfs_buf_log_item对象

```
STATIC void _xfs_trans_bjoin(struct xfs_trans *tp, struct xfs_buf *bp,
			     int reset_recur)
{
	struct xfs_buf_log_item *bip;
	// 给这个 xfs_buf 初始化一个 log_item，日志项
	xfs_buf_item_init(bp, tp->t_mountp);
	// xfs_buf 的日志项
	bip = bp->b_log_item;
	if (reset_recur)
		bip->bli_recur = 0;

	/*
	 * Take a reference for this transaction on the buf item.
	 */
	atomic_inc(&bip->bli_refcount);

	/*
	 * Attach the item to the transaction so we can find it in
	 * xfs_trans_get_buf() and friends.
	 * // 将日志项加入到事务中
	 */
	xfs_trans_add_item(tp, &bip->bli_item);
	bp->b_transp = tp;
}
```

#### 修改agf元数据，xfs_alloc_log_agf

afg元数据的变化，将事务中记录的log_item设置为dirty

```
/*
 * Log the given fields from the agf structure.
 */
void xfs_alloc_log_agf(
	xfs_trans_t *tp, /* transaction pointer */
	xfs_buf_t *bp, /* buffer for a.g. freelist header */
	int fields) /* mask of fields to be logged (XFS_AGF_...) */
{
	......
	// 这个 tracepoint 可以拿到 agf 更新后的内容
	trace_xfs_agf(tp->t_mountp, bp->b_addr, fields, _RET_IP_);
	// 用于标识特定类型的缓冲区日志格式。在这里，“BLFT”代表“Buffer Log Format Type”，“AGF”代表“Allocation Group Footer”
	xfs_trans_buf_set_type(tp, bp, XFS_BLFT_AGF_BUF);

	xfs_btree_offsets(fields, offsets, XFS_AGF_NUM_BITS, &first, &last);
	// 修改已经在 tran 事务的 log item 链表中的 item 为 dity
	xfs_trans_log_buf(tp, bp, (uint)first, (uint)last);
}
```

设置xfs_log_item为DIRTY

```
void xfs_trans_dirty_buf(struct xfs_trans *tp, struct xfs_buf *bp)
{
	struct xfs_buf_log_item *bip = bp->b_log_item;
	......
	if (bip->bli_flags & XFS_BLI_STALE) {
		bip->bli_flags &= ~XFS_BLI_STALE;
		ASSERT(bp->b_flags & XBF_STALE);
		bp->b_flags &= ~XBF_STALE;
		bip->__bli_format.blf_flags &= ~XFS_BLF_CANCEL;
	}
	bip->bli_flags |= XFS_BLI_DIRTY | XFS_BLI_LOGGED;

	tp->t_flags |= XFS_TRANS_DIRTY;
	// 设置这个 log_item 的 flag 为 dirty
	set_bit(XFS_LI_DIRTY, &bip->bli_item.li_flags);
}
```

#### xfs_trans_commit/xfs_log_commit_cil

`xfs_trans_commit` 是 XFS 文件系统中用于提交事务的函数。它负责将事务中的所有修改记录到日志中，并确保这些修改最终应用到文件系统中。

- xfs_log_item表示需要记录到日志中的一个文件系统元数据操作，它包含了描述该操作的具体内容，比如 inode 修改、目录项更新、空间分配信息（AGF、AGI更改）等。

- `xfs_log_vec` 结构体则用于管理实际写入日志的数据。它包括了指向一个或多个 `xfs_log_iovec` 结构的指针，这些 `xfs_log_iovec` 结构包含了实际要被写入到日志的数据。`xfs_log_vec` 是日志写入过程中的一个容器，它聚合了多个日志条目的数据，以便一次性地进行有效的日志写操作。每个 `xfs_log_item` 可以关联一个或多个 `xfs_log_vec`。通常，一个 `xfs_log_item` 对应一个 `xfs_log_vec`，但在某些情况下，一个日志项可能需要多个 `xfs_log_vec` 来完整地描述它的所有数据变更。例如，如果一个日志项涉及到多个不连续的元数据区块的更新，就可能需要多个 `xfs_log_vec` 来描述这些更新。

  在事务提交过程中，`xfs_log_item` 提供了关于元数据更改的所有必要信息，而 `xfs_log_vec` 则负责将这些信息格式化并准备好写入到磁盘上的日志区。日志写入过程首先是通过 `xfs_log_item` 的信息来构建一个或多个 `xfs_log_vec`，然后这些 `xfs_log_vec` 被送到日志子系统进行物理写入。

- 将格式化后的xfs_log_item插入到cil链表中，这样同一个xfs_log_item既会在事务链表中，也会在xfs_cil链表中。

  ```
  	list_for_each_entry (lip, &tp->t_items, li_trans) {
  		/* Skip items which aren't dirty in this transaction. */
  		if (!test_bit(XFS_LI_DIRTY, &lip->li_flags))
  			continue;
  		......
  		if (!list_is_last(&lip->li_cil, &cil->xc_cil))
  			// xfs_log_item 不在 cil 链表的尾部
  			// 在这里将 tran 中的 items 移到 cil 中，其实一个 xfs_log_item 在 tran 链表中，也会加入到 cil 链表中，在 xfs_log_item 被格式化之后
  			// 不过后面 tran 被释放了
  			list_move_tail(&lip->li_cil, &cil->xc_cil);
  	}
  ```

- 调用**xlog_cil_push_background**触发工作队列m_cil_workqueue的cli的工作xc_push_work，执行函数xlog_cil_push_work

  ```
  	if (cil->xc_push_seq < cil->xc_current_sequence) {
  		cil->xc_push_seq = cil->xc_current_sequence;
  		// 在工作队列 m_cil_workqueue 上调度一个工作，这个工作会调用 xlog_cil_push_work 函数
  		queue_work(log->l_mp->m_cil_workqueue, &cil->xc_push_work);
  	}
  ```

- 在函数__xfs_trans_commit函数中将事务释放

#### xlog_ticket，日志票据

在 XFS 文件系统中，日志票据（`xlog_ticket`）用于管理日志空间的分配和使用。日志票据的主要作用是跟踪和管理磁盘日志空间的使用情况，确保文件系统在执行各种操作（如文件改名、创建、删除等）时有充足的日志空间来记录这些操作。

1. **管理日志空间**：`xlog_ticket` 记录了一个事务所需的日志空间大小，确保在日志中为该事务保留足够的空间。
2. **跟踪事务进度**：它跟踪事务的进度，包括已经使用的日志空间和剩余的日志空间。
3. **协调日志写入**：`xlog_ticket` 协调多个事务对日志空间的使用，避免出现日志空间不足的情况。
4. **防止日志溢出**：通过管理和分配日志空间，`xlog_ticket` 能有效防止日志溢出，确保文件系统的稳定性和一致性。

相关函数

- `xfs_log_regrant`，函数用于重新授予日志空间（regrant log space）给一个已经存在的日志票据（log ticket）。当一个事务需要更多的日志空间时，可以使用这个函数来增加日志票据的空间。
- xfs_log_reserve 函数用于为一个新的事务分配日志空间。该函数确保在开始事务之前有足够的日志空间来记录事务操作。计算事务所需的日志空间；检查日志中是否有足够的空间；如果有足够的空间，为事务分配日志空间，并创建一个日志票据。
- xfs_log_ticket_regrant 函数用于重新分配日志票据中的日志空间。这个函数通常在一个事务已经使用完最初分配的日志空间，但需要更多空间时使用。检查新的日志空间需求。如果有足够的日志空间，重新分配并更新日志票据。
- xfs_log_ticket_ungrant 函数用于释放一个日志票据所占用的日志空间。这个函数在事务完成或取消时使用。释放日志票据占用的日志空间。更新日志票据状态和相关计数器。

#### xfs_buf_item_format格式化日志项

FS 文件系统中用于格式化缓冲区日志项（buffer log item）的函数。这个函数的主要作用是将 `xfs_log_item` 记录的内容（例如元数据修改）格式化并写入到 `xfs_log_vec` 中，以便后续可以将这些日志条目写入到日志中，确保文件系统的一致性。

```
STATIC void
xfs_buf_item_format(
    struct xfs_log_item    *lip,
    struct xfs_log_vec     *lv)
```

对于一个保存 AGF 信息的 `xfs_buf`，`bli_format_count` 的值取决于这个缓冲区中有多少个 `xfs_buf_log_format` 结构需要记录修改信息。通常情况下，AGF 信息占用一个块，因此只需要一个 `xfs_buf_log_format` 结构来记录这块的修改信息，<u>所以 `bli_format_count` 的值通常是 1</u>。

```
       fallocate-73144 [006] .... 30231.112269: xfs_buf_item_format: (xfs_buf_item_format+0x0/0x520 [xfs])
       fallocate-73144 [006] .... 30231.112317: <stack trace>
 => xfs_buf_item_format
 => xfs_log_commit_cil
 => __xfs_trans_commit
 => xfs_alloc_file_space
 => xfs_file_fallocate
 => vfs_fallocate
 => ksys_fallocate
 => __x64_sys_fallocate
 => do_syscall_64
 => entry_SYSCALL_64_after_hwframe
```

#### xlog_cil_push_work

`xlog_cil_push_work` 是一个内核函数，用于将 CIL（Committed Item List）中的日志项推送到磁盘日志中。

1. **获取 CIL 上下文**：获取当前的 CIL 上下文。
2. **创建新的日志记录**：将 CIL 中的所有脏的日志项打包成一个新的日志记录。
3. **格式化日志**：格式化这些日志项以便写入磁盘。
4. **写入磁盘**：调用 `submit_bio` 或类似的函数将日志记录写入磁盘。
5. **更新状态**：更新 CIL 和事务的状态。
6. **插入 AIL 链表**：将已提交的日志项插入到 AIL（Active Item List）中，以便后续处理。

#### checkpoint事务

heckpoint 事务（也称为 CIL (Committed Item List) 提交）是一个关键机制，用于保证文件系统的一致性和高效日志管理。CIL 是一个链表，用于暂存已经提交但尚未写入磁盘的日志项（`xfs_log_item`）。

#### xlog_write

`xlog_write` 函数负责将日志向量链表的数据写入日志文件。它管理日志记录的格式化、空间分配、及实际写入磁盘的操作。

#### start_lsn和tail_lsn的计算

```
be64_to_cpu(iclog->ic_header.h_lsn);

xfs_lsn_t tail_lsn = xlog_assign_tail_lsn(log->l_mp);
```

其实就是iclog->ic_header中的h_lsn和h_tail_lsn

#### 给xfs_in core log设置tail_lsn

遍历 AIL 链表，找到最小的 LSN

```
xfs_lsn_t xlog_assign_tail_lsn_locked(struct xfs_mount *mp)
{
	......
	lip = xfs_ail_min(mp->m_ail);
	if (lip)
		tail_lsn = lip->li_lsn;
	else
		tail_lsn = atomic64_read(&log->l_last_sync_lsn);
```

设置当前状态是：想要同步这个iclog；不再写入

```
static bool __xlog_state_release_iclog(struct xlog *log,
				       struct xlog_in_core *iclog)
{
	.......
	if (iclog->ic_state == XLOG_STATE_WANT_SYNC) {
		/* update tail before writing to iclog */
		xfs_lsn_t tail_lsn = xlog_assign_tail_lsn(log->l_mp);
		// 开始写入 iclog
		iclog->ic_state = XLOG_STATE_SYNCING;
		iclog->ic_header.h_tail_lsn = cpu_to_be64(tail_lsn);
		xlog_verify_tail_lsn(log, iclog, tail_lsn);
		/* cycle incremented when incrementing curr_block */
		return true;
	}

	ASSERT(iclog->ic_state == XLOG_STATE_ACTIVE);
	return false;
}
```

#### xlog_cil_insert_format_items

`xlog_cil_insert_format_items` 函数用于将格式化的日志项插入到 CIL 中。

1. **获取 CIL 上下文**：获取当前事务的 CIL 上下文。
2. **格式化日志项**：将事务中的修改项格式化为日志项。
3. **插入 CIL**：将格式化的日志项插入到 CIL。
4. **更新状态**：更新 CIL 的状态，例如增加已用空间。

#### 各个格式化函数的功能逻辑

这些函数用于将不同类型的日志项格式化，以便记录到日志或 CIL 中。

`xfs_bud_item_format`

用于格式化 "buffer update done"（BUD）日志项。这些项记录缓冲区更新已完成。

`xfs_buf_item_format`

用于格式化缓冲区日志项。这些项记录缓冲区的修改。

`xfs_qm_dquot_logitem_format`

用于格式化配额（quota）日志项。这些项记录配额管理相关的修改。

`xfs_qm_qoff_logitem_format`

用于格式化配额关闭（quota off）日志项。这些项记录配额系统关闭的操作。

`xfs_icreate_item_format`

用于格式化 inode 创建日志项。这些项记录新的 inode 创建操作。

`xfs_inode_item_format`

用于格式化 inode 日志项。这些项记录 inode 的修改。

#### xfs_log持久化的状态

```
/*
 * In core log state
 */
enum xlog_iclog_state {
	XLOG_STATE_ACTIVE, /* Current IC log being written to */
	XLOG_STATE_WANT_SYNC, /* Want to sync this iclog; no more writes 当 core log 准备好要同步到磁盘时，它会进入此状态。这表示不再向日志写入新的条目，并且日志准备好要写入磁盘。*/
	XLOG_STATE_SYNCING, /* This IC log is syncing 当 core log 正在被写入磁盘时，它会处于此状态。这表示同步过程正在进行中，日志条目正在从内存缓冲区传输到磁盘存储*/
	XLOG_STATE_DONE_SYNC, /* Done syncing to disk 当 core log 成功同步到磁盘后，它会进入此状态。这表示所有日志条目已写入磁盘，并且日志已准备好可以再次使用*/
	XLOG_STATE_CALLBACK, /* Callback functions now 当 core log 处于此状态时，它表示正在执行与日志相关的回调函数。这些回调函数可能包括通知其他组件日志已同步或执行其他与日志相关的操作*/
	XLOG_STATE_DIRTY, /* Dirty IC log, not ready for ACTIVE status 当 core log 包含未同步的条目时，它会处于此状态。这表示日志尚未准备好可以再次写入，需要先同步到磁盘*/
	XLOG_STATE_IOERROR, /* IO error happened in sync'ing log 如果在同步 core log 时遇到 I/O 错误，它会进入此状态。这表示同步过程由于 I/O 错误而失败，可能需要采取进一步的操作来处理错误并恢复日志的一致性*/
};
```

#### xlog_state_set_callback

cli的in core log写入磁盘后后触发ail

```
static void xlog_state_set_callback(struct xlog *log,
				    struct xlog_in_core *iclog,
				    xfs_lsn_t header_lsn)
{
	// 状态从 XLOG_STATE_DONE_SYNC 切换到 XLOG_STATE_CALLBACK
	iclog->ic_state = XLOG_STATE_CALLBACK;

	.......
	atomic64_set(&log->l_last_sync_lsn, header_lsn); // 将日志写入的lsn设置为l_last_sync_lsn
	// cil in core log 写入磁盘后，就插入 ail 列表，同时调用 wake_up_process 唤醒 xfsalid 内核线程
	xlog_grant_push_ail(log, 0);
}
```

#### xlog_state_done_syncing

xfs in core log写入磁盘完成，将iclog的状态改为同步完成，同时开始进行callback，同时会唤醒在ic_write_wait等待的task，让其继续运行

```
STATIC void xlog_state_done_syncing(struct xlog_in_core *iclog)
{
	struct xlog *log = iclog->ic_log;

	/*
	 * If we got an error, either on the first buffer, or in the case of
	 * split log writes, on the second, we shut down the file system and
	 * no iclogs should ever be attempted to be written to disk again.
	 */
	if (!XLOG_FORCED_SHUTDOWN(log)) {
		// xfs 没有被强制 shutdonw，且状态是 XLOG_STATE_SYNCING，修改状态为 XLOG_STATE_DONE_SYNC
		ASSERT(iclog->ic_state == XLOG_STATE_SYNCING);
		iclog->ic_state = XLOG_STATE_DONE_SYNC;
	}
	// 唤醒一个等待的task
	wake_up_all(&iclog->ic_write_wait);
	.......
	xlog_state_do_callback(log);
}
```

可以在这里加入一个kprobe，用来输出iclog->ic_header的h_lsn和h_tail_lsn，

#### xlog_ioend_work

这是一个工作队列函数，一个bio完成，回调该函数通知写入完成，通知xfs in core log写入磁盘完成

#### xfs_alloc_vextent

##### 功能

`xfs_alloc_vextent` 函数用于分配一个或多个虚拟扩展（extent），这是 XFS 文件系统中块分配的核心函数之一。该函数根据指定的分配参数（如目标 AG、所需块数等）尝试分配实际的磁盘块。

##### 逻辑

1. **初始化分配环境**：
   - 初始化分配请求的参数和环境，包括目标 AG、所需块数、分配策略等。
2. **搜索空闲块**：
   - 在指定的 AG 中搜索满足条件的空闲块。
3. **执行块分配**：
   - 根据搜索结果执行块分配操作，并更新相关的元数据结构。
4. **返回结果**：
   - 返回分配结果，包括分配的块号和状态。

#### xfs_alloc_fix_freelist

#### struct xfs_buf

**`xfs_buf`** 是 XFS 文件系统中的缓冲区结构，**用于管理和缓存特定的磁盘块（block）**，包括元数据块和数据块。`xfs_buf` 结构与文件系统的低级 I/O 操作紧密结合。

当文件系统执行读操作时，Page Cache 会首先检查所请求的数据是否在缓存中。如果在缓存中，则直接返回数据。

如果数据不在 Page Cache 中，则文件系统会从磁盘读取数据。这时，`xfs_buf` 可能被用来读取整个块，并将数据加载到 Page Cache 中。

当文件系统执行写操作时，数据首先写入 Page Cache。内核会在适当的时候将 Page Cache 中的数据刷新到磁盘。

XFS 文件系统使用 `xfs_buf` 来管理这些写入操作，确保数据完整性和一致性。

#### struct xfs_perag 与 struct xfs_agf 的关系

`xfs_perag` 和 `xfs_agf` 都与 XFS 文件系统的分配组（AG，Allocation Group）相关，但它们的作用和内容不同。

`xfs_perag` 结构用于在内存中管理和维护每个分配组的元数据。它包含了分配组的运行时信息，如空闲块计数、引用计数等。是内存运行时的结构。

`xfs_agf` （Allocation Group Free）是磁盘上的结构，包含分配组的空闲块信息。`xfs_agf` 结构存储在每个分配组的 AGF 块中，并记录该分配组中的空闲空间信息。是磁盘上持久化结构，当文件系统挂载时，xfs_agf的内容会被读取并加载到xfs_perag中。xfs_agf的信息会存放在xfs_buf中，

#### struct xfs_log_vec

#### xlog_state_release_iclog

#### xlog_sync

#### xfs_log_release_iclog

#### xfs_log_worker

它是xfs日志系统的一部分，用来处理xfs日志的后台工作。这个函数主要负责将内存中的日志数据写入磁盘，管理日志缓冲区，以及确保文件系统事务记录的持久性和一致性。

1. **日志提交**：将内存中的日志记录提交到磁盘，以确保文件系统的元数据更新和事务操作被持久化。
2. **日志空间管理**：管理日志缓冲区的使用情况，确保有足够的空间来记录新的事务。
3. **日志回放和恢复**：在系统启动时，处理可能需要回放的日志，以恢复文件系统的一致性。
4. **周期性触发**：定期触发日志提交操作，以减少延迟和提高性能。

```
void xfs_log_work_queue(struct xfs_mount *mp)
{
	//激活一次延迟任务，l_work 每次都要 kzalloc
	// fs.xfs.xfssyncd_centisecs = 3000，难道这里 30 秒才同步一次？
	queue_delayed_work(mp->m_sync_workqueue, &mp->m_log->l_work,
			   msecs_to_jiffies(xfs_syncd_centisecs * 10));
}
```

xfs文件系统mount成功后，会定时触发一个延迟任务，alloc_workqueue("xfs-sync/%s"，这个任务负责将iclog写入磁盘

```
           <...>-472567 [002] .... 63701.551899: xlog_write_iclog: (xlog_write_iclog+0x0/0x2b0 [xfs])
           <...>-472567 [002] .... 63701.553793: <stack trace>
 => xlog_write_iclog
 => xlog_state_release_iclog
 => __xfs_log_force_lsn
 => xfs_log_force_lsn
 => xfs_file_fsync
 => do_fsync
 => __x64_sys_fsync
 => do_syscall_64
 => entry_SYSCALL_64_after_hwframe
     kworker/5:1-472198 [005] .... 63715.063773: xlog_write_iclog: (xlog_write_iclog+0x0/0x2b0 [xfs])
     kworker/5:1-472198 [005] .... 63715.064450: <stack trace>
 => xlog_write_iclog
 => xlog_state_release_iclog
 => xfs_log_force
 => xfs_log_worker
 => process_one_work
 => worker_thread
 => kthread
 => ret_from_fork
```

#### iop_push、xfsaild_push_item

将xfs_log_item对应的xfs_buf数据写入delay write queue

返回值

```
#define XFS_ITEM_SUCCESS 
#define XFS_ITEM_PINNED 1
#define XFS_ITEM_LOCKED 2
#define XFS_ITEM_FLUSHING 3
```

- **`XFS_ITEM_SUCCESS`**：表示 AGF 日志条目已成功推送，可以继续处理下一个条目。日志项已成功推送并已从 AIL 中移除，日志项已经成功地刷新到磁盘或其他适当的目标位置，没有任何阻塞条件。
- **`XFS_ITEM_FLUSHING`**：表示 AGF 日志条目正在写入，可能需要等待或进行其他处理。这个状态表示日志项已被标记为正在进行 I/O 操作（通常是写操作），并且还没有完成。其他线程应该等待刷新操作完成。
- **`XFS_ITEM_PINNED`**：表示 AGF 日志条目被固定，无法推送，需要等待依赖操作完成。日志项被固定在内存中，通常是因为其引用计数器（reference count）没有降到零，意味着某些操作（如正在进行的 I/O 或其他持有该项的引用）阻止了它的释放。
- **`FS_ITEM_LOCKED`**：表示 AGF 日志条目被锁定，无法推送，需要等待解锁。 日志项处于被锁定状态，可能是因为正在被其他操作使用，或者它的状态需要先被更新。需要等到锁释放后才能继续对其进行推送操作。

#### xfs_buf_delwri_queue

将xfs_buf加入delay write queue中，xfs_buf成员b_list是链表的节点。

#### __xfs_buf_submit

如果agf的对应的xfs_buf不存在，会调用该函数从磁盘读取，而且是同步读取，XBF_READ

序列化agf的xfs_buf的元数据到磁盘，是提交delay write queue，这是使用XBF_ASYNC|XBF_WRITE，异步写

#### tail_lsn是在哪里设置的

ic_data->hic_header->h_tail_lsn这个lsn是在哪里赋值的，xlog_sync时会设置tail_lsn，标识已经完成的事务

ic_data->hic_header->h_lsn这个lsn是commit_lsn，这个lsn应该等于ctx->start_lsn

这个commit_lsn在函数xfs_trans_ail_update_bulk中设置给xfs_log_item->li_lsn

```
void
xfs_trans_ail_update_bulk(
	struct xfs_ail		*ailp,
	struct xfs_ail_cursor	*cur,
	struct xfs_log_item	**log_items,
	int			nr_items,
	xfs_lsn_t		lsn) __releases(ailp->ail_lock)
{
	......
		// 在这里给 xfs_log_item 设置了日志写入的 lsn，这个 lsn 是日志写入的 lsn
		lip->li_lsn = lsn;
		// 将 xfs_log_item 的 ail 链表节点 加入 tmp 链表中
		list_add(&lip->li_ail, &tmp);
    ......
}
```

### 验证

这是systemtap输出agf-0元数据提交后bio回调日志，输出了agf-0的元数据基本信息

```
xfs_buf_item_done, lip:0xffff9cfd4b736e70, agf_magic:0x58414746, agf_freeblks:63980, agf_longest:63976, agf_lsn:(1,1875), agf_crc:0xe82a97b7
```

直接查看磁盘元数据，可以看到lsn对应(1,1875) = "00 00 00 01 00 00 07 53"，crc接在后面，“f9 ec”表示63980， “f9 e8”表示63976

```
 ⚡ root@localhost  ~  hexdump -C -s 512 -n 512 /dev/loop0
00000200  58 41 47 46 00 00 00 01  00 00 00 00 00 00 fa 00  |XAGF............|
00000210  00 00 00 01 00 00 00 02  00 00 00 00 00 00 00 01  |................|
00000220  00 00 00 01 00 00 00 00  00 00 00 01 00 00 00 04  |................|
00000230  00 00 00 04 00 00 f9 ec  00 00 f9 e8 00 00 00 00  |................|
00000240  d8 1b 9f 1f e7 cb 4c 88  81 e4 04 96 d7 bd 4f 6f  |......L.......Oo|
00000250  00 00 00 00 00 00 00 01  00 00 00 05 00 00 00 01  |................|
00000260  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
000002d0  00 00 00 01 00 00 07 53  e8 2a 97 b7 00 00 00 00  |.......S.*......|
000002e0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00000400
```

一个ag大小的计算方式：agsize=64000 blks * 4096 bytes/blk = 262144000 bytes，ag中有多少块可用xfs_info去查看

AGF0偏移 = 超级块偏移 + 0 * AG大小 = 512

AGF1偏移 = 超级块偏移 + 1 * AG大小 = 512 + 262144000 = 262144512，hexdump -C -s 262144512 -n 512 /dev/loop0

AGF2偏移 = 超级块偏移 + 2 * AG大小 = 512 + 524288000 = 524288512，hexdump -C -s 524288512 -n 512 /dev/loop0

### 资料

[内核基础设施——wait queue - Notes about linux and my work (laoqinren.net)](https://linux.laoqinren.net/kernel/wait-queue/)

[workqueue（linux kernel 工作队列） - 知乎 (zhihu.com)](https://zhuanlan.zhihu.com/p/469007029)

[xfs文件系统-日志相关结构 - 知乎 (zhihu.com)](https://zhuanlan.zhihu.com/p/682959500)


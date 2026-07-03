# Vector / Tile 重命名方案

本文档描述 RV-TCore 在标量 GPR 重命名之外，为 **Vector（VRAT + VPRF）** 与 **Tile（TRAT + TPRF）** 扩展的重命名机制，包括分配、回收、checkpoint 与 flush 恢复。

---

## 1. 设计原则

1. **分域独立 RAT**：GPR / Vector / Tile 各自维护 RAT、free list、checkpoint，互不混用 ptag 空间
2. **Rename 集中管理**：重命名逻辑集中在 `RenameUnit`，RegFile 单元只负责 busy/读写
3. **Commit 更新 CRAT**：退休时更新 `m_vcrat` / `m_tcrat`，回收 `o_ptag` 到 free list
4. **Wakeup 按域匹配**：`WakeupEvent.rtyp` 区分 GPR/VEC/TILE，Issue 分域唤醒

---

## 2. 架构寄存器与物理寄存器

| 域 | 架构寄存器 | 默认物理容量 | 初始映射 |
|----|-----------|-------------|----------|
| GPR | x0–x31（32） | 192 | x0→p0，其余由 crat 初始化 |
| Vector | v0–v31（32） | 64 | v[i]→p[i]（1:1） |
| Tile | t0–t7（8） | 16 | t[i]→p[i]（1:1） |

物理编号在各域内**局部编号**，通过 `rtyp` 区分，不做全局编码。

---

## 3. RenameUnit 扩展结构

路径：`include/rename/rename_unit.h`，`modules/frontend/rename_unit.cpp`

### 3.1 新增成员

```cpp
// Vector
std::array<i32, RV_VREG_SIZE>  m_vrat;
std::array<i32, RV_VREG_SIZE>& m_vcrat;      // commit 态，Top 持有
std::deque<i32>                m_vfree_list;
VRegFileUnit&                  m_vregfile;

// Tile
std::array<i32, RV_TILE_SIZE>  m_trat;
std::array<i32, RV_TILE_SIZE>& m_tcrat;
std::deque<i32>                m_tfree_list;
TRegFileUnit&                  m_tregfile;
```

### 3.2 分派逻辑（R1 阶段）

```
if (inst->type == VECTOR)  → rename_vector(inst)
else if (inst->type == TILE) → rename_tile(inst)
else                         → rename_gpr(inst)
```

### 3.3 流水线阶段（与标量一致）

| 阶段 | 功能 |
|------|------|
| R0 | 从 `decode_rename_q` 取指进入 `m_r1_buf` |
| R1 | RAT 查找、源/目的寄存器重命名、freelist 分配 |
| R2 | ROB 分配、LQ/SQ 分配、dispatch 到 `rename_issue_q` |

---

## 4. GPR 重命名（既有逻辑）

```cpp
// 源寄存器
rs1.ptag = m_rat[rs1.atag]
rs1.ready = m_regfile.is_ready(rs1.ptag)

// 目的寄存器（atag != 0）
ptag = m_free_list.pop_front()
m_regfile.set_busy(ptag)
rd.ptag = ptag
rd.o_ptag = m_rat[rd.atag]
m_rat[rd.atag] = ptag
```

x0 特殊处理：`rd.ptag = 0`，`m_rat[0] = 0`，不参与 free list。

---

## 5. Vector 重命名

### 5.1 源操作数

```cpp
rename_src_vec(vs1):  vs1.ptag = m_vrat[vs1.atag], ready = vregfile.is_ready(ptag)
rename_src_vec(vs2):  同上

// VX 类指令的标量操作数仍走 GPR
rename_src_gpr(rs1):  rs1.ptag = m_rat[rs1.atag], ready = regfile.is_ready(ptag)
```

### 5.2 目的寄存器 vd

**LMUL = 1（默认）**

```cpp
ptag = m_vfree_list.pop_front()
vregfile.set_busy(ptag)
vd.ptag = ptag
vd.o_ptag = m_vrat[vd.atag]
vd.rgrp = 1
m_vrat[vd.atag] = ptag
```

**LMUL > 1（寄存器组）**

一次分配连续 `group` 个物理寄存器，并更新组内所有逻辑寄存器的 VRAT：

```cpp
allocate_vec_group(vd, lmul, vd.atag):
  head_ptag = vfree_list.pop_front()
  vregfile.set_busy(head_ptag)
  vrat[base_atag] = head_ptag
  for i in 1..group-1:
    extra = vfree_list.pop_front()
    vregfile.set_busy(extra)
    vrat[base_atag + i] = extra
```

R1 阶段在分配前检查：`m_vfree_list.size() >= need`（`need = lmul` 或 1）。

### 5.3 v0 特殊处理

- v0 映射到固定物理寄存器 **p0**
- 当前实现：v0 参与普通 rename 路径时 `atag==0` 走固定映射
- mask-off（`vm=0`）时隐式读 v0 的完整建模留待后续与 CSR 协同

### 5.4 free list 重建

```cpp
rebuild_vfree_list():
  used[0..capacity) = false
  used[0] = true
  for ptag in m_vrat: used[ptag] = true
  free_list = { p | p >= RV_VREG_SIZE && !used[p] }
```

架构寄存器占用的物理 0..31 不进入 free list；额外物理寄存器 32..capacity-1 作为重命名空间。

---

## 6. Tile 重命名

### 6.1 策略

采用与 Vector 一致的 **全重命名** 策略（策略 A）：

```cpp
rename_src_tile(ts1/ts2)
td.ptag = tfree_list.pop_front()
tregfile.set_busy(ptag)
td.o_ptag = m_trat[td.atag]
m_trat[td.atag] = ptag
```

### 6.2 备选策略（未实现）

| 策略 | 说明 | 适用场景 |
|------|------|----------|
| A 全重命名 | 每条写 td 分配新物理寄存器 | 通用、与 GPR 模型一致（**当前采用**） |
| B 架构绑定 | TRAT 固定，仅 load/store/move 切换 | 累加器型 Tile，rename 压力小 |

---

## 7. Decode 与 Rename 衔接

`DecodeUnit` 对 Vector 指令（`is_vector(op)`）在 decode 末尾调用 `map_vector_operands()`：

| funct3 | 语义 | 操作数映射 |
|--------|------|-----------|
| 000 | VV | vs1←rs1, vs2←rs2, vd←rd |
| 100 | VX | rs1 保留为 GPR 标量, vs2←rs2, vd←rd |
| 011 | VI | vs2←rs2, vd←rd（立即数在 imm） |

`InstType` 设为 `VECTOR`，Rename R1 走 `rename_vector()`。

Tile 指令解码尚未接入，测试中需手动设置 `InstType::TILE` 与 `ts1/ts2/td`。

---

## 8. Checkpoint 与 Flush

### 8.1 RatCheckpoint 扩展

```cpp
struct RatCheckpoint {
    std::array<int, RV_GPR_SIZE>  rat;
    std::array<int, RV_VREG_SIZE> vrat;      // 新增
    std::array<int, RV_TILE_SIZE> trat;      // 新增
    std::deque<int> free_list;
    std::deque<int> vfree_list;               // 新增
    std::deque<int> tfree_list;               // 新增
    ROBID rid;
    u64 c_id;
};
```

分支指令在 R1 分配 ROB 后，若 `is_branch(opcode)`，保存完整 GPR + Vector + Tile 快照。

### 8.2 Flush 恢复

**FlushScope::ALL**

```
m_rat = m_crat
m_vrat = m_vcrat
m_trat = m_tcrat
rebuild_free_list / rebuild_vfree_list / rebuild_tfree_list
```

**分支错误预测恢复（recover_c_id）**

1. 回收 `m_r2_buf` 与 `rename_issue_q` 中已分配未提交的 ptag（GPR/VEC/TILE 分域）
2. 从 checkpoint 恢复 `rat/vrat/trat` 与三个 free list
3. 删除该 checkpoint 及更年轻的 checkpoint

---

## 9. Commit 阶段

路径：`modules/backend/commit_unit.cpp`

### 9.1 写回（inst_resolve → write_back）

| 指令类型 | 写回目标 | 队列 |
|----------|----------|------|
| VECTOR + vd.valid | `vd_payload` → VPRF | `vec_reg_req_q` |
| TILE + td.valid | `td_payload` → TPRF | `tile_reg_req_q` |
| GPR + rd.valid | `rd.val` → PRF | `reg_req_q` |

### 9.2 退休（retire_inst）

```cpp
// GPR
m_crat[rd.atag] = rd.ptag
rename.recycle_phys(rd.o_ptag)

// Vector
m_vcrat[vd.atag] = vd.ptag
rename.recycle_vec_phys(vd.o_ptag, vd.rgrp)

// Tile
m_tcrat[td.atag] = td.ptag
rename.recycle_tile_phys(td.o_ptag)
```

### 9.3 LMUL 组回收（待完善）

当前 `recycle_vec_phys` 仅回收组头 ptag；完整实现需在 commit 时根据 `rgrp` 回收组内所有成员 ptag，或维护组元数据表。

---

## 10. Issue 与 Wakeup

### 10.1 发射队列

| 队列 | 指令类型 |
|------|----------|
| `m_int_q` | INT, BRANCH |
| `m_ls_q` | LOAD, STORE, STA, STD |
| `m_fp_q` | FP |
| `m_vec_q` | VECTOR |
| `m_tensor_q` | TILE |

### 10.2 就绪判断（MicroOp::ready）

- GPR：`rs1/rs2.ready`（`rtyp == GPR`）
- Vector：`vs1/vs2.ready`
- Tile：`ts1/ts2.ready`

### 10.3 Wakeup 匹配规则

```cpp
WakeupEvent.rtyp == GPR  → 匹配 rs1/rs2.ptag
WakeupEvent.rtyp == VEC  → 匹配 vs1/vs2/vd.ptag
WakeupEvent.rtyp == TILE → 匹配 ts1/ts2/td.ptag
```

### 10.4 寄存器读请求

| 操作数 | 函数 | 队列 |
|--------|------|------|
| GPR 源 | `EmitSrcRead` | `reg_req_q` |
| Vector 源 | `EmitVecSrcRead` | `vec_reg_req_q` |
| Tile 源 | `EmitTileSrcRead` | `tile_reg_req_q` |

---

## 11. 配置与容量规划

| 参数 | 建议范围 | 当前默认 | 说明 |
|------|----------|----------|------|
| `vprf_capacity` | 48–96 | 64 | Vector 物理寄存器很贵 |
| `tprf_capacity` | 8–32 | 16 | Tile 寄存器更大 |
| `rename_width` | 4 | 4 | 与标量同宽 |
| `vec_issue_width` | 1–2 | 1 | Vector 发射宽度 |
| `tile_issue_width` | 1 | 1 | Tile 发射宽度 |

Rename 停顿条件（R1）：

- GPR：`rd.valid && free_list.empty()`
- Vector：`vd.valid && vfree_list.size() < lmul`
- Tile：`td.valid && tfree_list.empty()`

---

## 12. 测试覆盖

| 测试 | 路径 | 覆盖点 |
|------|------|--------|
| `rename_test` | `tests/module/rename_test.cpp` | GPR rename 端到端 |
| `vector_regfile_test` | `tests/module/vector_regfile_test.cpp` | VRAT 分配、VPRF 写回 wakeup |
| `commit_test` | `tests/module/commit_test.cpp` | 含 vregfile/tregfile 依赖 |

---

## 13. 相关文档

- 寄存器堆实现：[VRegFile-TRegFile.md](./VRegFile-TRegFile.md)
- Vector 执行单元：[VectorCore-Design.md](./VectorCore-Design.md)
- Tensor / Tile 执行单元：[TensorCore-Design.md](./TensorCore-Design.md)

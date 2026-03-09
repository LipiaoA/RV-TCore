### 总结实现情况

- **DecodeUnit（指令解码单元）**
  - 在 `decode_unit.h` 中引入了 `FetchUnit` 与 `SInst`，增加：
    - 成员：`FetchUnit* m_fetch`、`std::deque<SInst> m_decoded_q`、`m_decode_width`。
    - 接口：`attach_fetch(FetchUnit*)`、`has_decoded()`、`try_pop_decoded(SInst&)`。
  - 在 `decode_unit.cpp` 中：
    - 保留并使用现有 `rv64i_decode`（含移位立即数修正）。
    - `DecodeUnit::tick()` 中调用 `m_fetch->try_dispatch()`，遍历 `FetchPacket::insts`，逐条用 `rv64i_decode` 解码成功后压入 `m_decoded_q`，每周期最多解码 `m_decode_width` 条。
    - `commit()` 当前为空实现，满足接口。

- **RenameUnit（寄存器重命名单元）**
  - 在 `rename_unit.h` 中：
    - 引入 `decode/inst.h`，定义 `kNumLogicalRegs=32`、`kNumPhysicalRegs=64`。
    - 增加成员：`std::vector<int> m_rat`、`std::deque<int> m_free_list`、`std::deque<SInst> m_decoded_q`、`std::deque<DynInst> m_renamed_q`、`m_rename_width`。
    - 对外接口：`push_from_decode(const SInst&)`、`has_renamed()`、`try_pop_renamed(DynInst&)`。
  - 在 `rename_unit.cpp` 中：
    - 构造函数初始化 RAT 为 1:1 映射，并将额外物理寄存器填入 `m_free_list`。
    - `tick()` 每周期从 `m_decoded_q` 取若干 `SInst`：
      - 根据 `SInst.type` 粗略判断是否需要 `rs1/rs2`、是否写回 `rd`，用 `m_rat` 映射出 `phys_rs1/phys_rs2`。
      - 为有写回且 `rd!=x0` 的指令从 `m_free_list` 分配新物理寄存器、更新 RAT，并填入 `phys_rd`。
      - 构造 `DynInst` 入 `m_renamed_q`，free-list 耗尽时会阻塞前端（把当前指令放回输入队列）。
    - `commit()` 预留，将来可接 ROB 做回收。

- **DispatchUnit（dispatch 单元）**
  - 在 `dispatch_unit.h` 中：
    - 引入 `decode/inst.h`，增加成员 `std::deque<DynInst> m_q` 与 `m_dispatch_width`。
    - 对外接口：`push_from_rename(const DynInst&)`、`has_ready()`、`try_issue_one(DynInst&)`。
  - 在 `dispatch_unit.cpp` 中：
    - `push_from_rename` 将 `DynInst` 入队。
    - `has_ready()` 判断队列是否非空，`try_issue_one` 从队头取出一条发射给下游。
    - `tick()/commit()` 暂不做额外处理，将 dispatch 视为“按需拉取”。

- **构建配置**
  - 在 `modules/frontend/CMakeLists.txt` 中，将 `decode_unit.cpp`、`dispatch_unit.cpp`、`rename_unit.cpp` 加入 `fetch` 库，并补充 include 目录 `include/decode`、`include/dispatch`、`include/rename`，确保三个单元参与编译并可被核心模块使用。

### 总结实现情况

- **DecodeUnit（指令解码单元）**
  - 在 `decode_unit.h` 中引入了 `FetchUnit` 与 `SInst`，增加：
    - 成员：`FetchUnit* m_fetch`、`std::deque<SInst> m_decoded_q`、`m_decode_width`。
    - 接口：`attach_fetch(FetchUnit*)`、`has_decoded()`、`try_pop_decoded(SInst&)`。
  - 在 `decode_unit.cpp` 中：
    - 保留并使用现有 `rv64i_decode`（含移位立即数修正）。
    - `DecodeUnit::tick()` 中调用 `m_fetch->try_dispatch()`，遍历 `FetchPacket::insts`，逐条用 `rv64i_decode` 解码成功后压入 `m_decoded_q`，每周期最多解码 `m_decode_width` 条。
    - `commit()` 当前为空实现，满足接口。

- **RenameUnit（寄存器重命名单元）**
  - 在 `rename_unit.h` 中：
    - 引入 `decode/inst.h`，定义 `kNumLogicalRegs=32`、`kNumPhysicalRegs=64`。
    - 增加成员：`std::vector<int> m_rat`、`std::deque<int> m_free_list`、`std::deque<SInst> m_decoded_q`、`std::deque<DynInst> m_renamed_q`、`m_rename_width`。
    - 对外接口：`push_from_decode(const SInst&)`、`has_renamed()`、`try_pop_renamed(DynInst&)`。
  - 在 `rename_unit.cpp` 中：
    - 构造函数初始化 RAT 为 1:1 映射，并将额外物理寄存器填入 `m_free_list`。
    - `tick()` 每周期从 `m_decoded_q` 取若干 `SInst`：
      - 根据 `SInst.type` 粗略判断是否需要 `rs1/rs2`、是否写回 `rd`，用 `m_rat` 映射出 `phys_rs1/phys_rs2`。
      - 为有写回且 `rd!=x0` 的指令从 `m_free_list` 分配新物理寄存器、更新 RAT，并填入 `phys_rd`。
      - 构造 `DynInst` 入 `m_renamed_q`，free-list 耗尽时会阻塞前端（把当前指令放回输入队列）。
    - `commit()` 预留，将来可接 ROB 做回收。

- **DispatchUnit（dispatch 单元）**
  - 在 `dispatch_unit.h` 中：
    - 引入 `decode/inst.h`，增加成员 `std::deque<DynInst> m_q` 与 `m_dispatch_width`。
    - 对外接口：`push_from_rename(const DynInst&)`、`has_ready()`、`try_issue_one(DynInst&)`。
  - 在 `dispatch_unit.cpp` 中：
    - `push_from_rename` 将 `DynInst` 入队。
    - `has_ready()` 判断队列是否非空，`try_issue_one` 从队头取出一条发射给下游。
    - `tick()/commit()` 暂不做额外处理，将 dispatch 视为“按需拉取”。

- **构建配置**
  - 在 `modules/frontend/CMakeLists.txt` 中，将 `decode_unit.cpp`、`dispatch_unit.cpp`、`rename_unit.cpp` 加入 `fetch` 库，并补充 include 目录 `include/decode`、`include/dispatch`、`include/rename`，确保三个单元参与编译并可被核心模块使用。
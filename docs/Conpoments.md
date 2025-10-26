#### 各组件简述



##### 1.取指单元

取指单元划分为5各阶段，分别是F0到F4：

F0：确定需要取指的PC并做分支预测

F1：从 F0 的预测结果确定要请求哪条 cache line

F2：检查ICache是否有 Ready line，若有则读入

F3：指令解码准备（拼接、预解码）从上一阶读到的数据中组装k_fetch_width条指令。

F4：将 F3 生成的 packet 放入 dispatch 队列



##### 2.L1-ICache设计



C++实现一个ICache模型,考虑DCache，或者L2 Cache等复用情况，支持可配置n路组相联，Cache Line大小可配置，支持每周期取多条指令，支持虚拟地址取指，使用TLB实现，非阻塞取指，Tag与Data分离，提高并行访问能力，替换策略使用LRU策略，参考BOOM项目的ICache实现。
Matrix Multiplication

矩阵乘法，包含大量 Load、Store、MUL、ADD。

主要压力：ALU、MUL、LSU、DCache。



Quick Sort

快速排序，包含比较、条件分支、数据交换。

主要压力：BRU、Branch Predictor、ALU、LSU。



Binary Search

二分查找，包含数据依赖、条件分支、随机访存。

主要压力：BRU、Branch Predictor、Load。



Histogram

对数组元素进行分类统计。

主要压力：Load、Store、地址计算、数据依赖。



Fibonacci / Recursive

递归或迭代实现，观察不同控制流和依赖结构。

主要压力：Branch、整数运算、寄存器压力。



Dot Product

向量点积，包含循环、Load、MUL、ADD。

主要压力：ALU、MUL、LSU、寄存器依赖。


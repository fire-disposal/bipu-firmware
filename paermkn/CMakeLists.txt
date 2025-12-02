# 声明这是一个 IDF 组件
idf_component_register(SRCS "my_lib.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       REQUIRES log) # 依赖系统日志库
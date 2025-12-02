# 声明 main 组件的源文件
idf_component_register(SRCS "main.c"
                       REQUIRES paermkn freertos) # 依赖自定义库 paermkn 和系统库 freertos
#交叉编译远程对端, 需要根据开发并IP地址修改
PEER			:= root@172.17.0.75

CC				:= $(CROSS_COMPILE)gcc
CXX				:= $(CROSS_COMPILE)g++
RM				:= rm -rf
PROJECT_PATH	:= $(shell pwd)
LVGL_PATH	 	:= $(PROJECT_PATH)/lvgl
LV_DRIVERS_PATH	:= $(PROJECT_PATH)/lv_drivers
# 目标文件
TARGET 			:= weather

# 宏定义
PD_MACROS 		+= _DEFAULT_SOURCE

# 额外Library, 不要带lib前缀和.so后缀
EXT_LIB			+= m
EXT_LIB			+= stdc++
EXT_LIB			+= drm
EXT_LIB			+= curl
EXT_LIB			+= cjson
# EXT_LIB			+= SDL2
# EXT_LIB			+= png
# EXT_LIB			+= turbojpeg
# EXT_LIB			+= freetype

# 额外库文件目录
# EXT_LIB_PATH	:=

# 源代码目录
SRC_PATHS		+= $(LVGL_PATH)				# LVGL库
SRC_PATHS		+= $(LV_DRIVERS_PATH)		# LVGL驱动
SRC_PATHS		+= src						# 代码
# 额外的Include目录
EXT_INC			+= $(PROJECT_PATH)
EXT_INC			+= $(SRC_PATHS)
EXT_INC			+= $(SYSROOT)/usr/include/libdrm

# 开启额外的编译警告
WARNINGS		:= -Wall -Wshadow -Wundef -Wmissing-prototypes -Wno-discarded-qualifiers -Wall -Wextra -Wno-unused-function -Wno-error=strict-prototypes -Wpointer-arith \
					-fno-strict-aliasing -Wno-error=cpp -Wuninitialized -Wmaybe-uninitialized -Wno-unused-parameter -Wno-missing-field-initializers -Wtype-limits -Wsizeof-pointer-memaccess \
					-Wno-format-nonliteral -Wno-cast-qual -Wunreachable-code -Wno-switch-default -Wreturn-type -Wmultichar -Wformat-security -Wno-ignored-qualifiers -Wno-error=pedantic \
					-Wno-sign-compare -Wno-error=missing-prototypes -Wdouble-promotion -Wclobbered -Wdeprecated -Wempty-body -Wtype-limits -Wshift-negative-value -Wstack-usage=2048 \
					-Wno-unused-value -Wno-unused-parameter -Wno-missing-field-initializers -Wuninitialized -Wmaybe-uninitialized -Wall -Wextra -Wno-unused-parameter \
					-Wno-missing-field-initializers -Wtype-limits -Wsizeof-pointer-memaccess -Wno-format-nonliteral -Wpointer-arith -Wno-cast-qual -Wmissing-prototypes \
					-Wunreachable-code -Wno-switch-default -Wreturn-type -Wmultichar -Wno-discarded-qualifiers -Wformat-security -Wno-ignored-qualifiers -Wno-sign-compare -std=c99

LDFLAGS 		+= $(addprefix -l, $(EXT_LIB))
ifdef DEBUG
	CFLAGS 		+= -Og -ggdb
else
	CFLAGS 		+= -O2 -g0 -z noexecstack
endif

ifdef SYSROOT
	CFLAGS		+= --sysroot $(SYSROOT)
endif

CFLAGS			+= $(addprefix -I, $(EXT_INC))
CFLAGS			+= $(addprefix -D, $(PD_MACROS))
CFLAGS			+= $(WARNINGS)

# Collect the files to compile
AEXT			:= .S
CEXT			:= .c
CXXEXT			:= .cpp

OBJEXT			:= .o

ASRCS			:= $(shell for path in $(SRC_PATHS); do find $$path -type f -name '*$(AEXT)';done)
CSRCS			:= $(shell for path in $(SRC_PATHS); do find $$path -type f -name '*$(CEXT)';done)
CXXSRCS			:= $(shell for path in $(SRC_PATHS); do find $$path -type f -name '*$(CXXEXT)';done)

AOBJS 			= $(ASRCS:$(AEXT)=$(OBJEXT))
COBJS 			= $(CSRCS:$(CEXT)=$(OBJEXT))
CXXOBJS			= $(CXXSRCS:$(CXXEXT)=$(OBJEXT))

MAINOBJ 		= ./main.o

OBJS 			= $(AOBJS) $(COBJS) $(CXXOBJS) $(MAINOBJ)

## MAINOBJ -> OBJFILES

.PHONY: all debug clean cross cross-debug cross-run

all: $(TARGET)
    
$(TARGET): $(OBJS)
	@$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

debug:
	@make clean
	@DEBUG=true make -j16

clean: 
	@$(RM) $(OBJS) $(TARGET)

ssh-key:
	@echo "正在生成密钥对，如果出现覆盖提示，请同意..."
	@test -f ~/.ssh/id_rsa.pub && echo "密钥对已经存在" || ssh-keygen -t rsa -f ~/.ssh/id_rsa -N ""
	@echo "正在将公钥保存到开发板..."
	@ssh $(PEER) "cat /root/.ssh/authorized_keys" | \
		grep -Fxf ~/.ssh/id_rsa.pub | \
		wc -l | \
		xargs test 0 -eq && \
		ssh $(PEER) "mkdir -p /root/.ssh && cat >> /root/.ssh/authorized_keys" < ~/.ssh/id_rsa.pub || \
		echo "公钥已经存在"

cross:
	@CROSS_COMPILE=toolchain/bin/arm-linux-gnueabihf- \
		SYSROOT=sysroot \
		make -j16
	@scp -O $(TARGET) $(PEER):/usr/bin/$(TARGET)

cross-debug:
	@CROSS_COMPILE=toolchain/bin/arm-linux-gnueabihf- \
		SYSROOT=sysroot \
		DEBUG=true \
		make -j16
	@scp -O $(TARGET) $(PEER):/usr/bin/$(TARGET)

cross-run:
	@ssh $(PEER) "nohup gdbserver 0.0.0.0:2000 /usr/bin/$(TARGET) >/dev/null 2>&1 &"

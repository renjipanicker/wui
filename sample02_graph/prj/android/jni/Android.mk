LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

####
PACKER := $(NDK_APP_OUT)/packer
PACKER_SRC := ../../../src/packer.cpp
$(PACKER): $(PACKER_SRC)
	echo "in packer target"
	mkdir -p $(NDK_APP_OUT)
	clang++ $(APP_CPPFLAGS) -o $(PACKER) $(PACKER_SRC)

####
jni/../$(NDK_APP_OUT)/html.cpp: ../../src/html.def $(PACKER)
	echo "in html target"
	$(PACKER) -d $(NDK_APP_OUT)/ -v html ../../src/html.def

####
LOCAL_MODULE := WuiDemoLib

LOCAL_C_INCLUDES += ../../../src/
LOCAL_C_INCLUDES += ../../../../src/
LOCAL_C_INCLUDES += $(NDK_APP_OUT)

LOCAL_SRC_FILES += ../$(NDK_APP_OUT)/html.cpp
LOCAL_SRC_FILES += ../../../src/main.cpp
LOCAL_SRC_FILES += ../../../../src/wui.cpp

LOCAL_CFLAGS := -g

LOCAL_LDLIBS += -llog -landroid
include $(BUILD_SHARED_LIBRARY)

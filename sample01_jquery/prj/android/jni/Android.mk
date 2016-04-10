LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

####
PACKER := $(NDK_APP_OUT)/packer
PACKER_SRC := ../../../src/packer.cpp
$(PACKER): $(PACKER_SRC)
		echo "in packer target"
		clang++ $(APP_CPPFLAGS) -o $(PACKER) $(PACKER_SRC)

####
HTML_SRC := ../../src/index.html ../../src/jquery-ui.css ../../src/jquery-ui.js ../../src/jquery.js ../../src/style.css
jni/../$(NDK_APP_OUT)/html.cpp: $(HTML_SRC) $(PACKER)
		echo "in html target"
		$(PACKER) -d $(NDK_APP_OUT)/ -v html -p html/ $(HTML_SRC)

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

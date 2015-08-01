agcc 是用来把 C code 交叉编译到 android 手机上，需要

- 32位的 ubuntu 系统
- NDK （在项目的 plugin 目录下）
- SDK （在项目的 plugin 目录下）

我的做法是: 

1. 新建一个虚拟机， 安装的是 ubuntu\-12.04.5\-i386 的桌面系统
2. 在 .bashrc 的最后加上：
```
export NDK_HOME=/home/andy/NDK/android-ndk-r4b
export PATH=$PATH:$NDK_HOME
export PATH=$PATH:$NDK_HOME/build/prebuilt/linux-x86/arm-eabi-4.4.0/bin
export SDK_HOME=/home/andy/SDK/android-sdk-linux
export PATH=$PATH:$SDK_HOME/platform-tools
```
3. 然后就可以直接 `agcc.pl test.c -o test` 编译代码了， 编译完成后用 sdk 中的 adb 工具把程序推送到手机， `adb push test /sdcard/test/`

***文中涉及的各种目录请根据个人情况修改***

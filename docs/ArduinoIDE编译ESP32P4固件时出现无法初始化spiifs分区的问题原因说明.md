# ArduinoIDE编译ESP32P4固件时出现无法初始化spiifs分区的问题原因说明

  我在使用 ArduinoIDE 编译 M5Stack TAB5 的程序的时候，在部分工程中出现无法找到 spiffs 分区的问题。下面记录一下原因。



## 编译环境

- Ubuntu24.04
- ArduinoIDE
- M5Stack TAB5



  在 ArduinoIDE 中，开发板选择 M5Tab5 之后，在工具栏下面是可以选择 Flash 配置分区的，Partition Scheme 选项就是选择 Flash分区的，默认的分区格式为 2*6.5MB app+3.6 MB SPIFFS，这里实际上对应的是 esp32 编译的 `partitions.csv` 文件。

  如果 Partition Scheme  中的选项无法满足当前工程的flash分区需求的时候，可以使用自定义分区的方式，就是在 Partition Scheme 中选择 Custom，然后在工程目录中创建一个自定义的 `partitions.csv`  文件。

  我在当前工程中创建的 `partitions.csv`  文件内容如下:

```bash
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  factory, 0x10000, 0x7E0000,
coredump, data, coredump,0x7F0000,0x10000,
```

  然后在当前工程中，flash分区格式选择的是 Custom，可以看到我自定义的flash分区中没有 spiffs 分区。

  后来，当我在当前工程中使用到 spiffs分区的时候，我直接在 `Partition Scheme `栏中选择了默认的分区，即`2*6.5MB app+3.6 MB SPIFFS`，然后将当前工程中的`partitions.csv`文件删除，重新编译并下载工程。这个时候问题出现了。我发现我还是无法找到 spiffs 分区。

  经过一番查找，找到了 ArduinoIDE 的临时编译目录，在Ubuntu中，在如下路径下：

```bash
/home/user/.cache/arduino/sketches/
```

  可以找到我这边的临时编译目录：

```bash
$ cd 070F9DEFA284D023408D34BC4356F796
$ ls -alh
总计 47M
drwxr-xr-x  5 qiao qiao 4.0K  9月 11 16:09 .
drwxr-xr-x 37 qiao qiao 4.0K  9月 11 16:07 ..
-rw-rw-r--  1 qiao qiao    0  9月 11 16:07 build_opt.h
-rw-r--r--  1 qiao qiao  710  9月 11 16:07 build.options.json
-rw-r--r--  1 qiao qiao 869K  9月 11 16:09 compile_commands.json
drwxr-xr-x  2 qiao qiao 4.0K  9月 11 16:09 core
-rw-rw-r--  1 qiao qiao    0  9月 11 16:09 file_opts
-rw-r--r--  1 qiao qiao  43K  9月 11 16:08 includes.cache
-rw-r--r--  1 qiao qiao    0  9月 11 16:07 .last-used
drwxr-xr-x 12 qiao qiao 4.0K  9月 11 16:09 libraries
-rw-r--r--  1 qiao qiao  842  9月 11 16:08 libraries.cache
-rw-rw-r--  1 qiao qiao 822K  9月 11 16:09 m5tab5_lvgl_show_jpeg_image.ino.bin
-rw-rw-r--  1 qiao qiao  21K  9月 11 16:07 m5tab5_lvgl_show_jpeg_image.ino.bootloader.bin
-rwxrwxr-x  1 qiao qiao  18M  9月 11 16:09 m5tab5_lvgl_show_jpeg_image.ino.elf
-rw-rw-r--  1 qiao qiao  13M  9月 11 16:09 m5tab5_lvgl_show_jpeg_image.ino.map
-rw-rw-r--  1 qiao qiao  16M  9月 11 16:09 m5tab5_lvgl_show_jpeg_image.ino.merged.bin
-rw-rw-r--  1 qiao qiao 3.0K  9月 11 16:09 m5tab5_lvgl_show_jpeg_image.ino.partitions.bin
-rw-rw-r--  1 qiao qiao  305  9月 11 16:07 partitions.csv
-rw-r--r--  1 qiao qiao  90K  9月 11 16:07 sdkconfig
drwxr-xr-x  2 qiao qiao 4.0K  9月 11 16:09 sketch
$ cat partitions.csv 
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  factory, 0x10000, 0x7E0000,
coredump, data, coredump,0x7F0000,0x10000,

```

  我发现，虽然在 ArduinoIDE 中更改了 flash 分区的设置，但是当从 Custom 分区方式改为默认分区的时候，编译目录下的 `partitions.csv`文件还是使用的之前的分区文件，并没有更新。

  于是，我手动将当前工程的临时编译路径删除，然后再Arduino中重新进行编译，这时候，ArduinoIDE 会在 `/home/user/.cache/arduino/sketches/` 重新生成一个新的临时编译目录，在新的目录下面，一切都是按照最新的配置进行编译的，对应的 partitions.csv` 文件也进行了更新。

```bash
$ cat partitions.csv 
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x640000,
app1,     app,  ota_1,   0x650000,0x640000,
spiffs,   data, spiffs,  0xc90000,0x360000,
coredump, data, coredump,0xFF0000,0x10000,
```

  在新的编译目录下完成编译之后，spiffs 文件系统初始化就正常了。



这个问题可能是当前M5Stack M5TAB5 工程编译适配的bug，需要手动清除临时编译目录才能解决。


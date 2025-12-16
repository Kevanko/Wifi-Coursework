# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/nm/esp/v5.5.1/esp-idf/components/bootloader/subproject"
  "/home/nm/Study/esp32s3/1w_ds18b20/build/bootloader"
  "/home/nm/Study/esp32s3/1w_ds18b20/build/bootloader-prefix"
  "/home/nm/Study/esp32s3/1w_ds18b20/build/bootloader-prefix/tmp"
  "/home/nm/Study/esp32s3/1w_ds18b20/build/bootloader-prefix/src/bootloader-stamp"
  "/home/nm/Study/esp32s3/1w_ds18b20/build/bootloader-prefix/src"
  "/home/nm/Study/esp32s3/1w_ds18b20/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/nm/Study/esp32s3/1w_ds18b20/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/nm/Study/esp32s3/1w_ds18b20/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()

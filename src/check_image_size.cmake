# fail the build when the firmware image no longer fits a partition slot.
# usage: cmake -DIMAGE=<file.bin> -DLIMIT_KB=<slot size in KiB> -P check_image_size.cmake

file(SIZE "${IMAGE}" image_bytes)
math(EXPR limit_bytes "${LIMIT_KB} * 1024")
math(EXPR image_kb "(${image_bytes} + 1023) / 1024")

if (image_bytes GREATER limit_bytes)
  message(FATAL_ERROR "firmware image is ${image_kb} KiB, larger than the ${LIMIT_KB} KiB partition slot (partition_table_rev7.json)")
endif ()

message(STATUS "firmware image ${image_kb} KiB of a ${LIMIT_KB} KiB partition slot")

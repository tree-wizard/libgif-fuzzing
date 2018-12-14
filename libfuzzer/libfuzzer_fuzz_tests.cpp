#include "GifTranscoder.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

extern "C" int LLVMFuzzerTestOneInput(const char *Data, size_t Size) {

  const char * pathOut = strdup("out.gif");
  GifTranscoder transcoder;
  int gifCode = transcoder.transcode(Data, pathOut);
  delete pathOut;
  return 0;
}
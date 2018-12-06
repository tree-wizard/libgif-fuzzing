#include "GifTranscoder.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  const char * pathIn = Data;
  const char * pathOut = "test.gif";
  GifTranscoder transcoder;
  int gifCode = transcoder.transcode(pathIn, pathOut);

  delete pathIn;
  delete pathOut;
  return 0;
}


#include "GifTranscoder.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  GifTranscoder transcoder;
  //int gifCode = transcoder.transcode(Data, pathOut);

  return 0;
}



look into the GIF encoding and decoding routines in gif_lib.h





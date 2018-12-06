#include "GifTranscoder.h"
#include <stdio.h> 
#include <stdint.h> 
#include <string.h>

int main(int argc, char * * argv) {
  const char * pathIn = strdup(argv[1]);
  const char * pathOut = strdup(argv[2]);
  GifTranscoder transcoder;
  int gifCode = transcoder.transcode(pathIn, pathOut);
  delete pathIn;
  delete pathOut;
  return 0;
}

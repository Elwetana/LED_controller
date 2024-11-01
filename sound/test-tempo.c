#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <aubio/aubio.h>


int main (int argc, char **argv)
{
  uint_t err = 0;
  if (argc < 2) {
    printf("no arguments, running tests\n");
    printf("usage: %s <source_path> [samplerate] [win_size] [hop_size]\n",
        argv[0]);
    return err;
  }
  uint_t samplerate = 0;
  if ( argc >= 3 ) samplerate = atoi(argv[2]);
  uint_t win_size = 1024; // window size
  if ( argc >= 4 ) win_size = atoi(argv[3]);
  uint_t hop_size = win_size / 4;
  if ( argc >= 5 ) hop_size = atoi(argv[4]);
  uint_t n_frames = 0, read = 0;

  char_t *source_path = argv[1];
  aubio_source_t * source = new_aubio_source(source_path, samplerate,
      hop_size);
  if (!source) { err = 1; goto beach; }

  if (samplerate == 0 ) samplerate = aubio_source_get_samplerate(source);

  // create some vectors
  fvec_t * in = new_fvec (hop_size); // input audio buffer
  fvec_t * out = new_fvec (1); // output position

  // create tempo object
  aubio_tempo_t * o = new_aubio_tempo("default", win_size, hop_size,
      samplerate);

  if (!o) { err = 1; goto beach_tempo; }

  do {
    // put some fresh data in input vector
    aubio_source_do(source, in, &read);
    float sum = 0;
    for(int frame = 0; frame < hop_size; ++frame) {
        sum += fvec_get_sample(in, frame);
    }
    printf("sum %f\n", sum);
    // execute tempo
    aubio_tempo_do(o,in,out);
    // do something with the beats
    if (out->data[0] != 0) {
      printf("beat at %.3fms, %.3fs, frame %d, %.2f bpm "
          "with confidence %.2f\n",
          aubio_tempo_get_last_ms(o), aubio_tempo_get_last_s(o),
          aubio_tempo_get_last(o), aubio_tempo_get_bpm(o),
          aubio_tempo_get_confidence(o));
    }
    n_frames += read;
  } while ( read == hop_size );

  printf("read %.2fs, %d frames at %dHz (%d blocks) from %s\n",
      n_frames * 1. / samplerate,
      n_frames, samplerate,
      n_frames / hop_size, source_path);

  // clean up memory
  del_aubio_tempo(o);
beach_tempo:
  del_fvec(in);
  del_fvec(out);
  del_aubio_source(source);
beach:
  aubio_cleanup();

  return err;
}


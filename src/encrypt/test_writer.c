#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "joye_writer.h"

#if defined(__linux__)
#define _stricmp strcasecmp
#endif

static const char *input = NULL;
static const char *output = NULL;
static const char *password = NULL;

void print_help(const char* name) {
      printf("usage:\n"
             "\t%s -in input -out output -p PASSWORD\n",
             name);
}

void parse_args(int argc, char *argv[]) {
  int i = 1;
  while (i < argc) {
    if (argv[i][0] != '-') {
      break;
    }

    if (_stricmp(argv[i], "-h") == 0) {
      print_help(argv[0]);
      exit(0);
    }

    if (_stricmp(argv[i], "-in") == 0) {
      input = argv[++i];
    } else if (_stricmp(argv[i], "-out") == 0) {
      output = argv[++i];
    } else if (_stricmp(argv[i], "-p") == 0) {
      password = argv[++i];
    }
    ++i;
  }
  if (!output) {
    output = "enc_test.bin";
  }
  if (!password) {
    password = "1234";
  }

  if (!input) {
      print_help(argv[0]);
      exit(0);
  }
}

int main(int argc, char *argv[]) {
  const unsigned char tags[] = "demo";
  parse_args(argc, argv);
  FILE *fp = fopen(output, "w+b");
  if (!fp) {
    perror("fopen");
    return 1;
  }

  enc_writer_ctx *w = enc_writer_new(fp, password, tags, (uint16_t)(sizeof(tags) - 1));
  if (!w) {
    fprintf(stderr, "Failed to create writer\n");
    fclose(fp);
    return 1;
  }

  if (input) {
    FILE *fi = fopen(input, "rb");
    if (!fi) {
      perror("fopen input");
      return 1;
    }
    char buf[1024];
    while (!feof(fi)) {
      int bytes = fread(buf, 1, sizeof(buf), fi);
      if (bytes > 0) {
        if (enc_writer_write(w, buf, bytes) != bytes) {
          fprintf(stderr, "Write failed (err=%d)\n", enc_writer_error(w));
          enc_writer_close(w);
          fclose(fi);
          return 1;
        }
      }
    }
    fclose(fi);
  } else {
    const char *message = "Hello from C writer!\n";
    if (enc_writer_write(w, message, strlen(message)) != strlen(message)) {
      fprintf(stderr, "Write failed (err=%d)\n", enc_writer_error(w));
      enc_writer_close(w);
      fclose(fp);
      return 1;
    }
  }

  if (enc_writer_close(w) != 0) {
    fprintf(stderr, "Flush/close failed\n");
    fclose(fp);
    return 1;
  }

  fclose(fp);
  printf("Encrypted file '%s' generated successfully.\n", output);
  return 0;
}

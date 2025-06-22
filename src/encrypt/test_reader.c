#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "joye_reader.h"

static const char *input = NULL;
static const char *output = NULL;
static const char *password = NULL;
static int32_t pos = 0;
static int recover = 0;

void print_help(const char *name) {
  printf("usage:\n"
         "\t%s -in input -out output -seek POS -p PASSWORD\n",
         name);
  exit(0);
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

    if (_strcmpi(argv[i], "-in") == 0) {
      input = argv[++i];
    } else if (_strcmpi(argv[i], "-out") == 0) {
      output = argv[++i];
    } else if (_strcmpi(argv[i], "-s") == 0) {
      pos = atoi(argv[++i]);
    } else if (_strcmpi(argv[i], "-p") == 0) {
      password = argv[++i];
    } else if (_strcmpi(argv[i], "-r") == 0) {
      recover = 1;
    }
    ++i;
  }
  if (!password) {
    password = "1234";
  }
  if (!input || !output) {
    print_help(argv[0]);
    exit(0);
  }
}

int main(int argc, char *argv[]) {
  const unsigned char tags[] = "demo";
  parse_args(argc, argv);

  if (!input) {
    print_help(argv[0]);
    exit(0);
  }
  int valid = enc_file_is_valid(input, password);
  printf("file valid: %s, %d\n", input, valid);
  if (valid != 1 && recover == 0) {
    fprintf(stderr, "Invalid file\n");
    return 1;
  }

  enc_reader_ctx *r = enc_reader_new();
  if (!r) {
    fprintf(stderr, "Failed to create writer\n");
    return 1;
  }
  int rc = enc_reader_open(r, input, password);
  if (rc != 1) {
    fprintf(stderr, "Failed to open file\n");
    return 1;
  }

  const char *file_tags = enc_reader_get_tags(r);
  if (file_tags) {
    if (_stricmp(file_tags, (char *)tags) != 0) {
      fprintf(stderr, "Invalid tags\n");
      return 1;
    }
  }
  
  if (pos > 0) {
      enc_reader_seek(r, pos, SEEK_SET);
  }

  // decrypt the input file
  {
    int isStdout = _stricmp(output, "stdout") == 0;
    FILE *out = isStdout ? stdout : fopen(output, "w+b");
    if (!out) {
      perror("fopen");
      return 1;
    }
    printf(".....\n");
    char buf[1024];
    for (;;) {
      int bytes = enc_reader_read(r, (unsigned char *)buf, sizeof(buf));
      if (bytes == 0) {
        printf("END OF FILE\n");
        break;
      }

      int writen = fwrite(buf, 1, bytes, out);
      if (writen != bytes) {
        fprintf(stderr, "Write failed\n");
        return 1;
      }
    }

    if (!isStdout) {
      fclose(out);
    }
  }

  if (enc_reader_close(r) != 0) {
    fprintf(stderr, "close failed\n");
    return 1;
  }

  return 0;
}

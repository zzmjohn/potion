//
// potion.c
// the Potion!
//
// (c) 2008 why the lucky stiff, the freelance professor
//
#include <stdio.h>
#include <sys/stat.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "potion.h"
#include "internal.h"
#include "opcodes.h"
#include "khash.h"
#include "table.h"

const char potion_banner[] = "potion " POTION_VERSION
                             " (date='" POTION_DATE "', commit='" POTION_COMMIT
                             "', platform='" POTION_PLATFORM "', jit=%d)\n";
const char potion_version[] = POTION_VERSION;

static void potion_cmd_usage() {
  printf("usage: potion [options] [script] [arguments]\n"
      "  -B, --bytecode     run with bytecode VM (slower, but cross-platform)\n"
      "  -X, --x86          run with x86 JIT VM (faster, x86 and x86-64)\n"
      "  -I, --inspect      print only the return value\n"
      "  -V, --verbose      show bytecode and ast info\n"
      "  -c, --compile      compile the script to bytecode\n"
      "  -h, --help         show this helpful stuff\n"
      "  -v, --version      show version\n"
      "(default: %s)\n",
#if POTION_JIT == 1
      "-X"
#else
      "-B"
#endif
  );
}

static void potion_cmd_stats(void *sp) {
  Potion *P = potion_create(sp);
  printf("sizeof(PN=%d, PNObject=%d, PNTuple=%d, PNTuple+1=%d, PNTable=%d)\n",
      (int)sizeof(PN), (int)sizeof(struct PNObject), (int)sizeof(struct PNTuple),
      (int)(sizeof(PN) + sizeof(struct PNTuple)), (int)(sizeof(struct PNTable) + sizeof(kh__PN_t)));
  printf("GC (fixed=%ld, actual=%ld, reserved=%ld)\n",
      PN_INT(potion_gc_fixed(P, 0, 0)), PN_INT(potion_gc_actual(P, 0, 0)),
      PN_INT(potion_gc_reserved(P, 0, 0)));
  potion_destroy(P);
}

static void potion_cmd_version() {
  printf(potion_banner, POTION_JIT);
}

static void potion_cmd_compile(char *filename, int exec, int verbose, void *sp) {
  PN buf;
  int fd = -1;
  struct stat stats;
  Potion *P = potion_create(sp);
  if (stat(filename, &stats) == -1) {
    fprintf(stderr, "** %s does not exist.", filename);
    goto done;
  }

  fd = open(filename, O_RDONLY);
  if (fd == -1) {
    fprintf(stderr, "** could not open %s. check permissions.", filename);
    goto done;
  }

  buf = potion_bytes(P, stats.st_size + 1);
  if (read(fd, PN_STR_PTR(buf), stats.st_size) == stats.st_size) {
    PN code;
    PN_STR_PTR(buf)[stats.st_size] = '\0';
    code = potion_source_load(P, PN_NIL, buf);
    if (PN_IS_PROTO(code)) {
      if (verbose > 1)
        printf("\n\n-- loaded --\n");
    } else {
      code = potion_parse(P, buf);
      if (verbose > 1) {
        printf("\n-- parsed --\n");
        potion_send(potion_send(code, PN_string), PN_print);
        printf("\n");
      }
      code = potion_send(code, PN_compile, potion_str(P, filename), PN_NIL);
      if (verbose > 1)
        printf("\n-- compiled --\n");
    }
    if (verbose > 1) {
      potion_send(potion_send(code, PN_string), PN_print);
      printf("\n");
    }
    if (exec == 1) {
      code = potion_vm(P, code, PN_NIL, 0, NULL);
      if (verbose > 1)
        printf("\n-- returned %lu --\n", code);
      if (verbose) {
        potion_send(potion_send(code, PN_string), PN_print);
        printf("\n");
      }
    } else if (exec == 2) {
#if POTION_JIT == 1
      PN val;
      PN_F func = potion_jit_proto(P, code, POTION_JIT_TARGET);
      val = func(P, PN_NIL, P->lobby);
      if (verbose > 1)
        printf("\n-- jit returned %p (fixed=%ld, actual=%ld, reserved=%ld) --\n", func,
          PN_INT(potion_gc_fixed(P, 0, 0)), PN_INT(potion_gc_actual(P, 0, 0)),
          PN_INT(potion_gc_reserved(P, 0, 0)));
      if (verbose) {
        potion_send(potion_send(val, PN_string), PN_print);
        printf("\n");
      }
#else
      fprintf(stderr, "** potion built without JIT support\n");
#endif
    } else {
      char pnbpath[255];
      FILE *pnb;
      sprintf(pnbpath, "%sb", filename);
      pnb = fopen(pnbpath, "wb");
      if (!pnb) {
        fprintf(stderr, "** could not open %s for writing. check permissions.", pnbpath);
        goto done;
      }

      code = potion_source_dump(P, PN_NIL, code);
      if (fwrite(PN_STR_PTR(code), 1, PN_STR_LEN(code), pnb) == PN_STR_LEN(code)) {
        printf("** compiled code saved to %s\n", pnbpath);
        printf("** run it with: potion %s\n", pnbpath);
        fclose(pnb);
      } else {
        fprintf(stderr, "** could not write all bytecode.");
      }
    }
  } else {
    fprintf(stderr, "** could not read entire file.");
  }

done:
  if (fd != -1)
    close(fd);
  if (P)
    potion_destroy(P);
}

int main(int argc, char *argv[]) {
  POTION_INIT_STACK(sp);
  int i, verbose = 0, exec = 1 + POTION_JIT;

  if (argc > 1) {
    for (i = 0; i < argc; i++) {
      if (strcmp(argv[i], "-I") == 0 ||
          strcmp(argv[i], "--inspect") == 0) {
        verbose = 1;
        continue;
      }

      if (strcmp(argv[i], "-V") == 0 ||
          strcmp(argv[i], "--verbose") == 0) {
        verbose = 2;
        continue;
      }

      if (strcmp(argv[i], "-v") == 0 ||
          strcmp(argv[i], "--version") == 0) {
        potion_cmd_version();
        return 0;
      }

      if (strcmp(argv[i], "-h") == 0 ||
          strcmp(argv[i], "--help") == 0) {
        potion_cmd_usage();
        return 0;
      }

      if (strcmp(argv[i], "-s") == 0 ||
          strcmp(argv[i], "--stats") == 0) {
        potion_cmd_stats(sp);
        return 0;
      }

      if (strcmp(argv[i], "-c") == 0 ||
          strcmp(argv[i], "--compile") == 0) {
        exec = 0;
      }

      if (strcmp(argv[i], "-B") == 0 ||
          strcmp(argv[i], "--bytecode") == 0) {
        exec = 1;
      }

      if (strcmp(argv[i], "-X") == 0 ||
          strcmp(argv[i], "--x86") == 0) {
        exec = 2;
      }
    }

    potion_cmd_compile(argv[argc-1], exec, verbose, sp);
    return 0;
  }

  fprintf(stderr, "// TODO: read from stdin\n");
  potion_cmd_usage();
  return 0;
}

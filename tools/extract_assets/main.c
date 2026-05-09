// tools/extract_assets/main.c
//
// One-shot extractor: reads two FILE000?.GLB archives, emits sprites,
// levels, demos, sounds, music, text into <output_dir>/.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void usage(const char *me) {
    fprintf(stderr,
        "usage: %s <FILE0000.GLB> <FILE0001.GLB> <output_dir>\n", me);
    exit(2);
}

int main(int argc, char **argv) {
    if (argc != 4) usage(argv[0]);
    const char *glb0 = argv[1];
    const char *glb1 = argv[2];
    const char *outdir = argv[3];
    (void)glb0; (void)glb1;
    mkdir(outdir, 0755);
    fprintf(stdout, "extract_assets: TODO. Output dir: %s\n", outdir);
    return 0;
}

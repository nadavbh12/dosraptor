// tools/extract_assets/main.c
//
// One-shot extractor: reads two FILE000?.GLB archives, emits sprites,
// levels, demos, sounds, music, text into <output_dir>/.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>

#include "GLBAPI.H"

/*
 * GLB_GetFileItems() is declared in glbapi.h after our addition.
 * GLB_InitSystem() signature (verified from GFX/GLBAPI.C):
 *
 *   INT GLB_InitSystem(CHAR *exepath, INT innum, CHAR *iprefix);
 *
 *   exepath   - path used to derive a search directory (strips after last '\')
 *   innum     - number of GLB files to open (e.g. 2 for FILE0000 + FILE0001)
 *   iprefix   - filename prefix, or NULL to use default "FILE"
 *
 * GLB_GetFileItems() added by us in GFX/GLBAPI.C:
 *
 *   INT GLB_GetFileItems(INT filenum);
 *
 *   Returns filedesc[filenum].items after GLB_InitSystem has run.
 *   GLB_NumItems() is PRIVATE (static) so it cannot be called from here.
 *
 * There is no GLB_End() / teardown function in the API.
 */

static void usage(const char *me) {
    fprintf(stderr,
        "usage: %s <FILE0000.GLB> <FILE0001.GLB> <output_dir>\n", me);
    exit(2);
}

int main(int argc, char **argv) {
    if (argc != 4) usage(argv[0]);

    const char *glb0   = argv[1];
    const char *glb1   = argv[2];
    const char *outdir = argv[3];

    /*
     * GLB_InitSystem discovers archives by constructing names like
     * FILE0000.glb, FILE0001.glb in either the CWD or the directory
     * derived from exepath (by stripping after the last '\').
     * On POSIX the backslash search never fires, so the whole exepath
     * string is used as a prefix — which would corrupt the constructed
     * filename.  The reliable fix is to chdir to the GLB directory
     * before calling GLB_InitSystem.
     *
     * We derive the directory from glb0; both archives are assumed to
     * live in the same directory (standard Raptor layout).
     */
    {
        /* dirname() may modify its argument; work on a copy. */
        char glb0_copy[4096];
        snprintf(glb0_copy, sizeof glb0_copy, "%s", glb0);
        const char *glb_dir = dirname(glb0_copy);
        if (chdir(glb_dir) != 0) {
            perror("chdir to GLB directory");
            return 1;
        }
    }

    /*
     * Open both archives.  innum=2 tells GLB_InitSystem to look for
     * FILE0000.glb (filenum=0) and FILE0001.glb (filenum=1).
     * Returns the count of archives actually opened.
     */
    int opened = GLB_InitSystem(argv[0], 2, NULL);
    if (opened < 1) {
        fprintf(stderr, "GLB_InitSystem: no archives opened (opened=%d)\n",
                opened);
        return 1;
    }

    int n0 = GLB_GetFileItems(0);
    int n1 = GLB_GetFileItems(1);

    fprintf(stdout, "Loaded %s: %d items\n", glb0, n0);
    fprintf(stdout, "Loaded %s: %d items\n", glb1, n1);

    if (n0 <= 0 || n1 <= 0) {
        fprintf(stderr,
            "Warning: one or more archives reported 0 items "
            "(n0=%d, n1=%d)\n", n0, n1);
    }

    /* Smoke test: print first 5 items from each archive. */
    fprintf(stdout, "\nFirst items in FILE0000.GLB:\n");
    for (int i = 0; i < 5 && i < n0; i++) {
        char name[64];
        DWORD handle;
        size_t sz;
        if (GLB_GetItemInfo(i, name, sizeof name, &handle, &sz) == 0) {
            fprintf(stdout, "  [%d] name=%-16s handle=0x%08lx size=%zu\n",
                    i, name, (unsigned long)handle, sz);
        }
    }
    fprintf(stdout, "\nFirst items in FILE0001.GLB:\n");
    for (int i = 0; i < 5 && i < n1; i++) {
        char name[64];
        DWORD handle;
        size_t sz;
        if (GLB_GetItemInfo(n0 + i, name, sizeof name, &handle, &sz) == 0) {
            fprintf(stdout, "  [%d] name=%-16s handle=0x%08lx size=%zu\n",
                    n0 + i, name, (unsigned long)handle, sz);
        }
    }

    /* Create output directory (ignore EEXIST). */
    mkdir(outdir, 0755);

    return 0;
}

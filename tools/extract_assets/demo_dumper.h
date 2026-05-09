#ifndef DEMO_DUMPER_H
#define DEMO_DUMPER_H

/* dump_demo_items() - extract all _REC (demo recording) items to JSON files.
 * Output: <outdir>/demos/<name>.json, one file per demo recording.
 * Returns the number of demo files written. */
int dump_demo_items(const char *outdir);

#endif /* DEMO_DUMPER_H */

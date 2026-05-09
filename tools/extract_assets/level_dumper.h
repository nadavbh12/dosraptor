#ifndef LEVEL_DUMPER_H
#define LEVEL_DUMPER_H

/* dump_level_items() - extract all _MAP (MAZELEVEL) items to JSON files.
 * Output: <outdir>/levels/<name>.json, one file per level.
 * Returns the number of level files written. */
int dump_level_items(const char *outdir);

#endif /* LEVEL_DUMPER_H */

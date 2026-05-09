#ifndef SPRITE_META_DUMPER_H
#define SPRITE_META_DUMPER_H

/* dump_sprite_meta_items() - extract all _ITM (SPRITE struct arrays) as JSON.
 * Output: <outdir>/sprites_meta/<name>.json, one file per _ITM item.
 * Returns the number of metadata files written. */
int dump_sprite_meta_items(const char *outdir);

#endif /* SPRITE_META_DUMPER_H */

#ifndef FOX_INGEST_H
#define FOX_INGEST_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char folder[512];
    char exe_path[512];
    int has_fox_dat;
    int has_adlib_dat;
    int has_voice_dat;
    int has_stage_dat;
    int has_vsprite_pat;
    int has_executable;
    int has_mt32_dat;
    int has_sc55_dat;
    int has_enhanced_music;
    int is_translated;
    int is_ready;
    char status_msg[256];
} IngestStatus;

/* Inspects a given path (folder or file), locates game assets and fills report */
int ingest_scan(const char *folder_or_file, IngestStatus *out);

#ifdef __cplusplus
}
#endif

#endif /* FOX_INGEST_H */

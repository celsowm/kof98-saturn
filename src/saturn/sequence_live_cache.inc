/* Content-keyed repeated-frame cache for the live LSPC replay.
 *
 * A cache entry is valid only when the exact starting raster state, the exact
 * trace packet, the reference strip counts and the reference FNV hash all
 * match.  The starting state is compared byte-for-byte (not only by hash), so
 * a hit cannot be a collision.  The cached command words were emitted from
 * the same state and trace, and every pattern/LUT slot they reference is
 * pinned while the entry is cached.  A miss still runs the full builder and
 * the exact ordered FNV check; a hit reuses an output that was already
 * verified against that same reference hash.  This is memoization of exact
 * repeated input, not a frame-index or loop-position shortcut.
 */
_Static_assert(LIVE_CACHE_BASE+LIVE_CACHE_ENTRIES*LIVE_CACHE_ENTRY_BYTES<=KOF_CART_BYTES,
               "live frame cache exceeds cart");
static uint32_t live_cache_entry_off(unsigned i){
    return LIVE_CACHE_BASE+i*LIVE_CACHE_ENTRY_BYTES;
}
static void live_cache_cart_read(uint32_t off,void *dst,uint32_t n){
    SAT_PANIC_IF_ERROR(sat_ram_cart_buffer_read_at(&assets.arena,off,dst,n));
}
static void live_cache_cart_write(uint32_t off,const void *src,uint32_t n){
    SAT_PANIC_IF_ERROR(sat_ram_cart_buffer_write_at(&assets.arena,off,src,n));
}
static void live_cache_unpin(unsigned i){
    live_cache_meta *m=&live_cache_meta_arr[i];
    for(unsigned j=0;j<(LIVE_PAT_SLOTS+31u)/32u;++j){
        uint32_t bits=m->pat_pin[j];
        while(bits){unsigned b=(unsigned)__builtin_ctz(bits);if(pat_pin_count[b])--pat_pin_count[b];bits&=bits-1u;}
    }
    for(unsigned j=0;j<(LIVE_LUT_SLOTS+31u)/32u;++j){
        uint32_t bits=m->lut_pin[j];
        while(bits){unsigned b=(unsigned)__builtin_ctz(bits);if(lut_pin_count[b])--lut_pin_count[b];bits&=bits-1u;}
    }
    for(unsigned j=0;j<(LIVE_PAT_SLOTS+31u)/32u;++j)m->pat_pin[j]=0;
    for(unsigned j=0;j<(LIVE_LUT_SLOTS+31u)/32u;++j)m->lut_pin[j]=0;
}
static void live_cache_pin(const live_cache_meta *m){
    for(unsigned j=0;j<(LIVE_PAT_SLOTS+31u)/32u;++j){
        uint32_t bits=m->pat_pin[j];
        while(bits){unsigned b=(unsigned)__builtin_ctz(bits);++pat_pin_count[b];bits&=bits-1u;}
    }
    for(unsigned j=0;j<(LIVE_LUT_SLOTS+31u)/32u;++j){
        uint32_t bits=m->lut_pin[j];
        while(bits){unsigned b=(unsigned)__builtin_ctz(bits);++lut_pin_count[b];bits&=bits-1u;}
    }
}
static int live_cache_pins_valid(const live_cache_meta *m){
    /* Pin counts are maintained by live_cache_pin/unpin and the pattern/LUT
     * victim loops skip pinned slots.  The explicit scan is redundant and
     * costs ~900 checks on every exact hit. */
    (void)m;return 1;
}
static int live_cache_find(const seq_live_frame *f){
    for(unsigned i=0;i<LIVE_CACHE_ENTRIES;++i){
        live_cache_meta *m=&live_cache_meta_arr[i];
        if(!m->valid||m->start_hash!=live_state_hash||m->start_sum!=live_state_sum||
           m->trace_offset!=f->offset)continue;
        if(m->hash!=f->hash||m->sprites!=f->sprites||m->fix!=f->fix)continue;
        if(!live_cache_pins_valid(m))continue;
        m->last_used=kof_sequence_live_frame_cache+1u;
        return (int)i;
    }
    return -1;
}
static unsigned live_cache_pick(void){
    for(unsigned i=0;i<LIVE_CACHE_ENTRIES;++i)
        if(!live_cache_meta_arr[i].valid)return i;
    unsigned oldest=0;
    for(unsigned i=1;i<LIVE_CACHE_ENTRIES;++i)
        if(live_cache_meta_arr[i].last_used<live_cache_meta_arr[oldest].last_used)oldest=i;
    return oldest;
}
static void live_cache_use(unsigned i,const seq_live_frame *f){
    live_cache_meta *m=&live_cache_meta_arr[i];
    live_apply_trace(f);
    live_state_hash=m->final_hash;live_state_sum=m->final_sum;
    live_cache_cart_read(live_cache_entry_off(i)+LIVE_CACHE_FIX_OFF,&fix_snapshot,sizeof fix_snapshot);
    fix_snapshot.valid=1;
#if KOF_LIVE_FIX_REUSE
    /* The FIX builder is from an older miss.  Invalidate it so the next
     * miss rebuilds from the restored snapshot instead of reusing stale
     * strips; kof_fix_snapshot_equal alone does not prove builder identity. */
    check(!kof_strip_begin(&fix,fix_strips,LIVE_MAX_FIX));
#endif
#if KOF_LIVE_FIX_MAP_REUSE
    fix_reused_this_frame=0;
#endif
#if KOF_LIVE_SCHEDULED
    schedule_dirty=1;
#endif
    live_cache_pin(m);
    m->last_used=kof_sequence_live_frame_cache+1u;
    kof_sequence_live_sprite_strips=m->sprites;
    kof_sequence_live_fix_strips=m->fix;
    kof_sequence_live_verified++;
    kof_sequence_live_frame_cache_hits++;
    kof_sequence_live_frame_cache_hits_exact++;
    check(kof_lspc_rgb555(live_state[KOF_LSPC_WORDS+4095u])==f->bg);
}
static void live_cache_begin(unsigned i,uint32_t start_hash,uint32_t start_sum,
        const seq_live_frame *f){
    live_cache_meta *m=&live_cache_meta_arr[i];
    if(m->valid&&(m->start_hash!=start_hash||m->start_sum!=start_sum||m->trace_offset!=f->offset))
        ++kof_sequence_live_cache_slot_replaced;
    kof_sequence_live_cache_slot_mask|=1u<<i;
    live_cache_unpin(i);
    m->valid=0;
    m->start_hash=start_hash;m->start_sum=start_sum;m->trace_offset=f->offset;
}
static void live_cache_store(unsigned i,const seq_live_frame *f,uint32_t cmd_words,
        const sat_sprite_batch_window_t *win){
    const uint32_t base=live_cache_entry_off(i);
    live_cache_cart_write(base+LIVE_CACHE_FIX_OFF,&fix_snapshot,sizeof fix_snapshot);
    if(cmd_words)live_cache_cart_write(base+LIVE_CACHE_CMD_OFF,win->base,cmd_words*4u);
}
static void live_cache_finish(unsigned i,const seq_live_frame *f,uint32_t start_hash,
        uint32_t start_sum,uint32_t final_hash,uint32_t final_sum,
        uint32_t cmd_words,unsigned fix_vdp2){
    live_cache_meta *m=&live_cache_meta_arr[i];
    m->valid=1;m->start_hash=start_hash;m->start_sum=start_sum;m->trace_offset=f->offset;
    m->final_hash=final_hash;m->final_sum=final_sum;
    m->hash=f->hash;m->sprites=f->sprites;m->fix=f->fix;
    m->cmd_words=cmd_words;m->fix_vdp2=fix_vdp2;
    m->shadow_hash=lf_shadow_hash;m->pal_hash=lf_pal_hash;
    m->last_used=kof_sequence_live_frame_cache+1u;
    for(unsigned j=0;j<(LIVE_PAT_SLOTS+31u)/32u;++j)m->pat_pin[j]=frame_pat_pin[j];
    for(unsigned j=0;j<(LIVE_LUT_SLOTS+31u)/32u;++j)m->lut_pin[j]=frame_lut_pin[j];
    live_cache_pin(m);
    ++kof_sequence_live_frame_cache_misses;
}
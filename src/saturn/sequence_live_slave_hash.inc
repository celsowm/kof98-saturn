/* Experimental parallel validation, using the pinned SDK mailbox.
 * Master owns all builders, VDP1/VDP2, cart and timers. The Slave is a
 * read-only consumer of completed strips until the matching response.
 * SH7604 cache is write-through but not snooping. Purge only the Slave's
 * local cache from cache-through code before reading the next snapshot.
 * This mirrors LibSaturn dbc679a src/hal/sh2/cache.cpp purge_through_alias;
 * deliberately no private C++ linkage or Master cache register writes. */
enum { LIVE_HASH_REQUEST=0x4b484153u,LIVE_HASH_REPLY=0x4b484152u,
       LIVE_HASH_TIMEOUT=60000u };
volatile uint32_t kof_sequence_live_slave_hash_jobs,kof_sequence_live_slave_hash_done;
volatile uint32_t kof_sequence_live_slave_hash_wait_ticks,kof_sequence_live_slave_hash_elapsed_ticks;
volatile uint32_t kof_sequence_live_slave_hash_errors;
static uint32_t live_hash_token,live_hash_sent_at;
static void __attribute__((noinline,noclone)) live_slave_purge(uint8_t mode){
    volatile uint8_t *ccr=(volatile uint8_t *)(uintptr_t)0xfffffe92u;
    *ccr=0x10u;*ccr=mode;
    __asm__ volatile("" ::: "memory");
}
static void live_hash_worker(void *unused){
    (void)unused;
    for(;;){
        if(sat_dual_sh2_slave_stop_requested())return;
        sat_dual_sh2_message_t request;
        if(sat_dual_sh2_slave_receive(&request)!=SAT_OK)continue;
        if(request.command!=LIVE_HASH_REQUEST)return;
        __asm__ volatile("" ::: "memory");
        const uint8_t ccr=sat_sh2_cache_control();
        if(ccr&1u){
            void (*purge)(uint8_t)=(void (*)(uint8_t))((uintptr_t)&live_slave_purge|0x20000000u);
            purge((uint8_t)(ccr&9u));
        }
        __asm__ volatile("" ::: "memory");
        uint32_t h=strip_hash(strip_hash(2166136261u,&sprites,0),&fix,524288u);
        /* Only mailbox payload crosses back; no cached shared writes. */
        __asm__ volatile("" ::: "memory");
        sat_dual_sh2_slave_heartbeat();
        if(sat_dual_sh2_slave_send(LIVE_HASH_REPLY,request.argument0,h)!=SAT_OK)return;
    }
}
static void live_hash_start(void){
    SAT_PANIC_IF_ERROR(sat_dual_sh2_configure_slave(live_hash_worker,0));
    SAT_PANIC_IF_ERROR(sat_dual_sh2_start(LIVE_HASH_TIMEOUT));
}
static void live_hash_dispatch(void){
    check(kof_sequence_live_slave_hash_jobs==kof_sequence_live_slave_hash_done);
    if(++live_hash_token==0u)++live_hash_token;
    live_hash_sent_at=ticks();
    __asm__ volatile("" ::: "memory");
    SAT_PANIC_IF_ERROR(sat_dual_sh2_send(LIVE_HASH_REQUEST,live_hash_token,0));
    ++kof_sequence_live_slave_hash_jobs;
}
static void live_hash_join(const seq_live_frame *f){
    sat_dual_sh2_message_t reply;
    const uint32_t start=ticks();
    sat_result_t r=sat_dual_sh2_wait_response(&reply,LIVE_HASH_TIMEOUT);
    const uint32_t now=ticks(),wait=now-start;
    kof_sequence_live_slave_hash_wait_ticks+=wait;
    kof_sequence_live_slave_hash_elapsed_ticks+=now-live_hash_sent_at;
    /* In parallel mode hash_ticks measures residual Master wait only.
     * Elapsed includes concurrent cache/emission work; not additive. */
    kof_sequence_live_hash_ticks+=wait;
    if(r!=SAT_OK||reply.command!=LIVE_HASH_REPLY||reply.argument0!=live_hash_token){
        ++kof_sequence_live_slave_hash_errors;check(0);
    }
    __asm__ volatile("" ::: "memory");
    verify_hash(f,reply.argument1);
    ++kof_sequence_live_slave_hash_done;
}

"""Actual C wooden-fish counting, coalescing and debounced NVS persistence."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class MuyuCoreC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        tmp=Path(cls.tmp.name)
        include=tmp/"include"
        include.mkdir()
        for name in ("esp_log.h","esp_timer.h","freertos/FreeRTOS.h","nvs.h"):
            path=include/name; path.parent.mkdir(exist_ok=True)
            path.write_text('#pragma once\n#include "fake_sdk.h"\n',encoding="utf-8")
        (include/"fake_sdk.h").write_text(r'''
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nabo_muyu.h"
typedef int esp_err_t;
typedef int nvs_handle_t;
typedef int portMUX_TYPE;
#define ESP_OK 0
#define ESP_FAIL -1
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define portMUX_INITIALIZER_UNLOCKED 0
static bool locked;
static void lock(portMUX_TYPE *mux) { (void)mux; assert(!locked); locked=true; }
static void unlock(portMUX_TYPE *mux) { (void)mux; assert(locked); locked=false; }
#define portENTER_CRITICAL(x) lock(x)
#define portEXIT_CRITICAL(x) unlock(x)
static int64_t now=10000000;
static int64_t esp_timer_get_time(void) { return now; }
static int read_opens,write_opens,commits,closes,warnings,sets;
static bool missing,fail_open,fail_set_count,fail_set_sound,fail_commit;
static bool inject_load_hit,inject_commit_hit;
static uint64_t persisted_count,staged_count;
static uint8_t persisted_sound=1,staged_sound;
static void warning(const char *tag,const char *fmt,int err) {
    assert(!locked && tag && fmt && err!=ESP_OK); ++warnings;
}
#define ESP_LOGW(...) warning(__VA_ARGS__)
static esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h) {
    assert(!locked && !strcmp(name,"nabo_muyu"));
    if(mode==NVS_READONLY) { ++read_opens; if(missing) return ESP_FAIL; }
    else { assert(mode==NVS_READWRITE); ++write_opens; if(fail_open) return ESP_FAIL; }
    *h=100+mode; return ESP_OK;
}
static esp_err_t nvs_get_u64(nvs_handle_t h,const char *key,uint64_t *out) {
    assert(!locked && h==100 && !strcmp(key,"count")); *out=persisted_count;
    if(inject_load_hit) { assert(nabo_muyu_hit()); assert(nabo_muyu_hit()); }
    return ESP_OK;
}
static esp_err_t nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *out) {
    assert(!locked && h==100 && !strcmp(key,"sound")); *out=persisted_sound; return ESP_OK;
}
static esp_err_t nvs_set_u64(nvs_handle_t h,const char *key,uint64_t value) {
    assert(!locked && h==101 && !strcmp(key,"count")); ++sets; staged_count=value;
    return fail_set_count ? ESP_FAIL : ESP_OK;
}
static esp_err_t nvs_set_u8(nvs_handle_t h,const char *key,uint8_t value) {
    assert(!locked && h==101 && !strcmp(key,"sound") && value<=1); ++sets; staged_sound=value;
    return fail_set_sound ? ESP_FAIL : ESP_OK;
}
static esp_err_t nvs_commit(nvs_handle_t h) {
    assert(!locked && h==101); ++commits;
    if(inject_commit_hit) { inject_commit_hit=false; assert(nabo_muyu_hit()); }
    if(fail_commit) return ESP_FAIL;
    persisted_count=staged_count; persisted_sound=staged_sound; return ESP_OK;
}
static void nvs_close(nvs_handle_t h) { assert(!locked && (h==100 || h==101)); ++closes; }
''',encoding="utf-8")
        harness=r'''
#include "fake_sdk.h"
#include "nabo_muyu.c"
int main(int argc,char **argv) {
    assert(argc==2); int which=atoi(argv[1]);
    if(which==1) { /* Default/missing NVS works completely offline. */
        missing=true; nabo_muyu_init(); assert(nabo_muyu_count()==0 && nabo_muyu_sound_enabled());
        assert(!nabo_muyu_wanted() && !nabo_muyu_hit());
        nabo_muyu_open(); assert(nabo_muyu_hit() && nabo_muyu_count()==1);
        assert(!write_opens && !commits);
    } else if(which==2) { /* Rapid clicks preserve every count, with one pending sound. */
        nabo_muyu_init(); nabo_muyu_open();
        for(int i=0;i<300;++i) assert(nabo_muyu_hit());
        assert(nabo_muyu_count()==300 && !write_opens);
        assert(nabo_muyu_take_hit() && !nabo_muyu_take_hit());
    } else if(which==3) { /* Muting discards pending audio, never the independent count. */
        nabo_muyu_init(); nabo_muyu_open(); assert(nabo_muyu_hit());
        nabo_muyu_set_sound(false); assert(!nabo_muyu_sound_enabled() && !nabo_muyu_take_hit());
        assert(nabo_muyu_hit() && nabo_muyu_count()==2 && !nabo_muyu_take_hit());
        nabo_muyu_set_sound(true); assert(!nabo_muyu_take_hit());
        assert(nabo_muyu_hit() && nabo_muyu_take_hit() && nabo_muyu_count()==3);
    } else if(which==4) { /* Closing cancels pending audio and rejects any further page hit. */
        nabo_muyu_init(); nabo_muyu_open(); nabo_muyu_hit(); nabo_muyu_close();
        assert(!nabo_muyu_wanted() && !nabo_muyu_take_hit() && !nabo_muyu_hit());
        assert(nabo_muyu_count()==1 && !write_opens);
    } else if(which==5) { /* No per-hit writes: idle debounce commits one snapshot. */
        nabo_muyu_init(); nabo_muyu_open(); for(int i=0;i<10;++i) nabo_muyu_hit();
        now+=1999999; nabo_muyu_idle_tick(); assert(!write_opens);
        ++now; nabo_muyu_idle_tick(); assert(write_opens==1 && commits==1 && persisted_count==10);
        now+=5000000; nabo_muyu_idle_tick(); assert(write_opens==1 && commits==1);
    } else if(which==6) { /* A new physical edge restarts idle debounce. */
        nabo_muyu_init(); nabo_muyu_open(); nabo_muyu_hit(); now+=1900000; nabo_muyu_hit();
        now+=100000; nabo_muyu_idle_tick(); assert(!write_opens);
        now+=1900000; nabo_muyu_idle_tick(); assert(commits==1 && persisted_count==2);
    } else if(which==7) { /* Explicit voice-owner exit flush does not wait two seconds. */
        nabo_muyu_init(); nabo_muyu_open(); nabo_muyu_hit(); nabo_muyu_close(); nabo_muyu_save();
        assert(commits==1 && persisted_count==1); nabo_muyu_save(); assert(commits==1);
    } else if(which==8) { /* Early UI edges during a slow NVS read are merged, not overwritten. */
        persisted_count=40; nabo_muyu_open(); nabo_muyu_hit(); inject_load_hit=true;
        nabo_muyu_init(); assert(nabo_muyu_count()==43 && !write_opens);
        nabo_muyu_save(); assert(persisted_count==43 && commits==1);
    } else if(which==9) { /* Already initialized state never reloads over live clicks. */
        persisted_count=20; nabo_muyu_init(); nabo_muyu_open(); nabo_muyu_hit();
        persisted_count=900; nabo_muyu_init(); assert(read_opens==1 && nabo_muyu_count()==21);
    } else if(which==10) { /* NVS stores all 64 bits across the 32-bit boundary. */
        persisted_count=UINT32_MAX; nabo_muyu_init(); nabo_muyu_open(); nabo_muyu_hit();
        assert(nabo_muyu_count()==UINT64_C(4294967296)); nabo_muyu_save();
        assert(persisted_count==UINT64_C(4294967296));
    } else if(which==11) { /* Counter saturation cannot wrap the lifetime count to zero. */
        persisted_count=UINT64_MAX; nabo_muyu_init(); nabo_muyu_open();
        assert(nabo_muyu_hit() && nabo_muyu_count()==UINT64_MAX && nabo_muyu_take_hit());
    } else if(which==12) { /* A UI hit arriving during commit retains a dirty newer revision. */
        nabo_muyu_init(); nabo_muyu_open(); nabo_muyu_hit(); inject_commit_hit=true;
        nabo_muyu_save(); assert(commits==1 && persisted_count==1 && nabo_muyu_count()==2);
        now+=2000000; nabo_muyu_idle_tick(); assert(commits==2 && persisted_count==2);
    } else if(which==13) { /* Failed commits are retried at idle cadence, not every audio tick. */
        nabo_muyu_init(); nabo_muyu_open(); nabo_muyu_hit(); fail_commit=true;
        now+=2000000; nabo_muyu_idle_tick(); assert(commits==1 && warnings==1);
        for(int i=0;i<100;++i) { now+=10000; nabo_muyu_idle_tick(); }
        assert(commits==1); fail_commit=false; now+=1000000; nabo_muyu_idle_tick();
        assert(commits==2 && persisted_count==1);
    } else if(which==14 || which==15 || which==16) { /* Any NVS boundary failure preserves retryability. */
        nabo_muyu_init(); nabo_muyu_open(); nabo_muyu_hit();
        fail_open=which==14; fail_set_count=which==15; fail_set_sound=which==16;
        nabo_muyu_save(); assert(warnings==1 && !commits && persisted_count==0);
        fail_open=fail_set_count=fail_set_sound=false; nabo_muyu_save();
        assert(commits==1 && persisted_count==1);
    } else if(which==17) { /* Independent sound preference is loaded and saved separately. */
        persisted_count=5; persisted_sound=0; nabo_muyu_init();
        assert(nabo_muyu_count()==5 && !nabo_muyu_sound_enabled());
        nabo_muyu_set_sound(true); assert(!write_opens); nabo_muyu_save();
        assert(persisted_count==5 && persisted_sound==1 && commits==1);
    } else if(which==18) { /* An invalid sound byte uses a safe default. */
        persisted_sound=7; nabo_muyu_init(); assert(nabo_muyu_sound_enabled());
        nabo_muyu_save(); assert(!write_opens);
    } else if(which==19) { /* An early explicit sound choice wins over a later saved snapshot. */
        persisted_sound=0; nabo_muyu_set_sound(true); nabo_muyu_init();
        assert(nabo_muyu_sound_enabled()); nabo_muyu_save(); assert(persisted_sound==1);
    } else assert(false);
    assert(!locked); return 0;
}
'''
        c=tmp/"muyu.c"; c.write_text(harness,encoding="utf-8")
        cls.exe=tmp/("muyu.exe" if os.name=="nt" else "muyu")
        result=subprocess.run([os.environ.get("CC","gcc"),"-std=c11","-Wall","-Wextra","-Werror",
                               "-I",str(include),"-I",str(ROOT/"firmware"),str(c),"-o",str(cls.exe)],
                              capture_output=True,text=True)
        if result.returncode: raise RuntimeError(result.stderr)

    def scenario(self,number):
        result=subprocess.run([str(self.exe),str(number)],capture_output=True,text=True,timeout=5)
        self.assertEqual(result.returncode,0,result.stderr)

    def test_missing_nvs_and_offline_count(self): self.scenario(1)
    def test_every_rapid_hit_counts_but_audio_is_coalesced(self): self.scenario(2)
    def test_sound_toggle_clears_audio_without_losing_counts(self): self.scenario(3)
    def test_close_rejects_hits_and_clears_pending_audio(self): self.scenario(4)
    def test_two_second_idle_commits_once(self): self.scenario(5)
    def test_new_hit_restarts_debounce(self): self.scenario(6)
    def test_voice_exit_flush_commits_immediately(self): self.scenario(7)
    def test_early_clicks_during_nvs_load_are_merged(self): self.scenario(8)
    def test_repeated_init_never_overwrites_current_count(self): self.scenario(9)
    def test_u64_persistence_crosses_u32_boundary(self): self.scenario(10)
    def test_u64_saturation_never_wraps(self): self.scenario(11)
    def test_ui_change_during_commit_remains_dirty(self): self.scenario(12)
    def test_failed_commit_has_bounded_idle_retry(self): self.scenario(13)
    def test_open_failure_preserves_dirty_state(self): self.scenario(14)
    def test_count_write_failure_preserves_dirty_state(self): self.scenario(15)
    def test_sound_write_failure_preserves_dirty_state(self): self.scenario(16)
    def test_sound_preference_is_independent(self): self.scenario(17)
    def test_invalid_saved_sound_uses_default(self): self.scenario(18)
    def test_early_sound_choice_wins_over_saved_state(self): self.scenario(19)


if __name__=="__main__": unittest.main(verbosity=2)

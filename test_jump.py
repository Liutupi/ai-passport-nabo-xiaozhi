from host_tools import CC, CXX, SHARED_EXPORT, unload_library
"""Exercise actual compiled game physics and render its real sprite composition."""
import ctypes
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parent
BOARD=ROOT/'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
class Game(ctypes.Structure):
    _fields_=[('phase',ctypes.c_int)]+[(n,ctypes.c_uint32) for n in ['rng','score','best','combo','changed_at','charge_at']]+[(n,ctypes.c_int) for n in ['dir','gap','skin','next_skin','distance','player_x','player_y','lift','character','future_dir','future_gap','future_skin','fall_contact_ms','fall_ground_drop','fall_target_x']]+[('pending_at',ctypes.c_uint32),('pending_hold',ctypes.c_int)]+[(n,ctypes.c_bool) for n in ['landed','centered','sound','pending_charge','pending_released']]+[(n,ctypes.c_int) for n in ['size','next_size','future_size','steps']]

temp=tempfile.TemporaryDirectory()
folder=Path(temp.name)
blob=(BOARD/'jump_sprites.bin').read_bytes()
(folder/'blob.c').write_text('const unsigned char sprites[] __asm__("_binary_jump_sprites_bin_start")={'+','.join(map(str,blob))+'};','ascii')
with (folder/'blob.c').open('a',encoding='ascii') as f:f.write('const unsigned char sounds[] __asm__("_binary_jump_sounds_bin_start")={'+','.join(map(str,(BOARD/'jump_sounds.bin').read_bytes()))+'};')
library=folder/'jump.dll'
subprocess.run([CC,'-std=gnu11','-O3','-shared',SHARED_EXPORT,'-Wall','-Wextra','-Werror','-I',str(BOARD),str(BOARD/'nabo_jump_model.c'),str(BOARD/'nabo_jump_rig.c'),str(BOARD/'nabo_jump_sound.c'),str(folder/'blob.c'),'-o',str(library)],capture_output=True,check=True)
lib=ctypes.CDLL(str(library))
for name in ['nabo_jump_reset','nabo_jump_press','nabo_jump_release','nabo_jump_tick','nabo_jump_draw_row']:
    getattr(lib,name).restype=None
lib.nabo_jump_reset.argtypes=[ctypes.POINTER(Game),ctypes.c_uint32,ctypes.c_uint32]
lib.nabo_jump_press.argtypes=lib.nabo_jump_release.argtypes=lib.nabo_jump_tick.argtypes=[ctypes.POINTER(Game),ctypes.c_uint32]
lib.nabo_jump_draw_row.argtypes=[ctypes.POINTER(Game),ctypes.c_uint32,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.POINTER(ctypes.c_uint16)]

def reset(seed=1,best=0):
    g=Game();lib.nabo_jump_reset(ctypes.byref(g),seed,best);return g
def step(g,t):lib.nabo_jump_tick(ctypes.byref(g),t)
def throw(g,start=1000,hold=None):
    hold=hold if hold is not None else (g.gap-4)*1000//150
    lib.nabo_jump_press(ctypes.byref(g),start)
    lib.nabo_jump_release(ctypes.byref(g),start+hold)
    return start+hold
def render(g,t):
    rows=[]
    for y in range(320):
        row=(ctypes.c_uint16*240)();lib.nabo_jump_draw_row(ctypes.byref(g),t,y,0,239,row)
        rows.extend(row)
    rgb=bytearray()
    for v in rows:rgb.extend(((v>>11)*255//31,((v>>5)&63)*255//63,(v&31)*255//31))
    return Image.frombytes('RGB',(240,320),bytes(rgb))

class Sound(ctypes.Structure):
    _fields_=[('position',ctypes.c_uint32),('step',ctypes.c_uint32),('offset',ctypes.c_uint),('length',ctypes.c_uint),('kind',ctypes.c_int),('last_sample',ctypes.c_int),('fade_from',ctypes.c_int),('fade_remaining',ctypes.c_uint)]
lib.nabo_jump_sound_start.argtypes=[ctypes.POINTER(Sound),ctypes.c_int,ctypes.c_uint]
lib.nabo_jump_sound_fill.argtypes=[ctypes.POINTER(Sound),ctypes.POINTER(ctypes.c_int16),ctypes.c_size_t]

class Tests(unittest.TestCase):
    def test_precomputed_edge_and_fall_stream_and_clear_tail(self):
        import struct
        raw = (BOARD/'jump_sounds.bin').read_bytes()
        for kind, offset, length in [(3, 76932, 1440), (4, 78372, 8000)]:
            expected = struct.unpack('<'+str(length)+'h', raw[offset*2:(offset+length)*2])
            sound = Sound();lib.nabo_jump_sound_start(ctypes.byref(sound),kind,1)
            actual=[]
            for count in [37, 319, 7, 511]*12:
                data=(ctypes.c_int16*count)();lib.nabo_jump_sound_fill(ctypes.byref(sound),data,count);actual.extend(data)
            self.assertEqual(actual[:length],list(expected))
            self.assertTrue(all(v==0 for v in actual[length:]))
            self.assertLessEqual(max(abs(v) for v in expected),5200)

    def test_original_jump_pcm_is_exact_across_chunk_boundaries(self):
        import struct
        raw=(BOARD/'jump_sounds.bin').read_bytes();expected=struct.unpack('<16055h',raw[35176*2:(35176+16055)*2])
        sound=Sound();lib.nabo_jump_sound_start(ctypes.byref(sound),1,1);actual=[]
        for count in [1,319,37,511,7]*20:
            data=(ctypes.c_int16*count)();lib.nabo_jump_sound_fill(ctypes.byref(sound),data,count);actual.extend(data)
        self.assertEqual(actual[:16055],list(expected));self.assertTrue(all(v==0 for v in actual[16055:]))

    def test_charge_loops_and_new_jump_replaces_old_audio(self):
        import struct
        raw=(BOARD/'jump_sounds.bin').read_bytes();expected=struct.unpack('<35176h',raw[86372*2:(86372+35176)*2])
        sound=Sound();lib.nabo_jump_sound_start(ctypes.byref(sound),5,1)
        data=(ctypes.c_int16*(35176+400))();lib.nabo_jump_sound_fill(ctypes.byref(sound),data,len(data))
        self.assertEqual(list(data[:35176]),list(expected));self.assertEqual(list(data[35176:]),list(expected[:400]))
        lib.nabo_jump_sound_start(ctypes.byref(sound),1,1)
        data=(ctypes.c_int16*320)();lib.nabo_jump_sound_fill(ctypes.byref(sound),data,320)
        self.assertEqual(data[0],expected[399])
        self.assertEqual(list(data[128:]),list(struct.unpack('<192h',raw[(35176+128)*2:(35176+320)*2])))
        self.assertLess(max(abs(v) for v in expected),20000)
        self.assertEqual(expected[0],0);self.assertEqual(expected[-1],0)

    def test_dirty_region_covers_every_changed_pixel(self):
        lib.nabo_jump_bounds.argtypes=[ctypes.POINTER(Game),ctypes.c_uint32]+[ctypes.POINTER(ctypes.c_int)]*4
        g=reset();previous=render(g,0);last=(Game.from_buffer_copy(g),0)
        lib.nabo_jump_press(ctypes.byref(g),100)
        for t in range(100,1400,40):
            if t==620:lib.nabo_jump_release(ctypes.byref(g),t)
            step(g,t);current=render(g,t)
            boxes=[]
            for game,when in [last,(g,t)]:
                vals=[ctypes.c_int() for _ in range(4)]
                lib.nabo_jump_bounds(ctypes.byref(game),when,*[ctypes.byref(v) for v in vals]);boxes.append([v.value for v in vals])
            x0,y0=min(b[0] for b in boxes),min(b[1] for b in boxes)
            x1,y1=max(b[2] for b in boxes),max(b[3] for b in boxes)
            before=previous.load();after=current.load()
            for y in range(320):
                for x in range(240):
                    if before[x,y]!=after[x,y]:self.assertTrue(x0<=x<=x1 and y0<=y<=y1,(t,x,y,boxes))
            previous=current;last=(Game.from_buffer_copy(g),t)

    def test_center_combo_and_best(self):
        g=reset();now=1000
        for combo in range(1,20):
            release=throw(g,now);step(g,release+400)
            self.assertEqual(g.phase,3);self.assertTrue(g.centered);self.assertEqual(g.combo,combo)
            self.assertEqual(g.score,combo*(combo+1));self.assertEqual(g.best,g.score)
            step(g,release+920);self.assertEqual(g.phase,0);self.assertIn(g.dir,[-1,1]);self.assertTrue(65<=g.gap<=110)
            now=release+1000

    def test_tight_scene_and_separate_charge_regions_cover_changed_pixels(self):
        for name in ['nabo_jump_scene_bounds', 'nabo_jump_character_bounds']:
            getattr(lib,name).argtypes=[ctypes.POINTER(Game),ctypes.c_uint32]+[ctypes.POINTER(ctypes.c_int)]*4
        for seed in [1, 7, 18]:
            g=reset(seed);last=(Game.from_buffer_copy(g),0);previous=render(g,0)
            lib.nabo_jump_press(ctypes.byref(g),100)
            for t in range(100,1500,40):
                if t==620:lib.nabo_jump_release(ctypes.byref(g),t)
                step(g,t);current=render(g,t)
                name='nabo_jump_scene_bounds' if last[0].phase==3 or g.phase==3 else 'nabo_jump_character_bounds'
                boxes=[]
                for game,when in [last,(g,t)]:
                    vals=[ctypes.c_int() for _ in range(4)]
                    getattr(lib,name)(ctypes.byref(game),when,*[ctypes.byref(v) for v in vals])
                    boxes.append([v.value for v in vals])
                x0,y0=min(b[0] for b in boxes),min(b[1] for b in boxes)
                x1,y1=max(b[2] for b in boxes),max(b[3] for b in boxes)
                bar=last[0].phase==1 or g.phase==1
                before=previous.load();after=current.load()
                for y in range(320):
                    for x in range(240):
                        if before[x,y]!=after[x,y]:
                            self.assertTrue(x0<=x<=x1 and y0<=y<=y1 or bar and 30<=x<=210 and 281<=y<=286,(seed,t,x,y,boxes))
                previous=current;last=(Game.from_buffer_copy(g),t)
    def test_edge_and_miss(self):
        g=reset();release=throw(g,hold=(g.gap+10-4)*1000//150);step(g,release+400)
        self.assertTrue(g.landed);self.assertFalse(g.centered);self.assertEqual(g.score,1)
        step(g,release+920);release=throw(g,3000,hold=(g.gap+35-4)*1000//150);step(g,release+400);self.assertEqual(g.phase,4)
        step(g,release+1200);self.assertEqual(g.phase,5);self.assertEqual(g.best,1)
        g.character=1;lib.nabo_jump_press(ctypes.byref(g),6000);self.assertEqual(g.phase,1);self.assertEqual(g.best,1);self.assertEqual(g.character,1)
    def test_tap_stays_on_current_platform(self):
        g=reset();release=throw(g,hold=0);step(g,release+600);self.assertEqual(g.phase,0);self.assertEqual(g.score,0)
    def test_fall_continues_flight_velocity_without_freezing(self):
        g=reset();release=throw(g,hold=900);step(g,release+400)
        last_lift=g.lift
        for age in range(20,561,20):
            step(g,release+400+age)
            self.assertLessEqual(g.lift,last_lift)
            if age<g.fall_contact_ms:self.assertGreater(last_lift-g.lift,0)
            self.assertLessEqual(last_lift-g.lift,20)
            last_lift=g.lift
        self.assertEqual(g.phase,5)
        self.assertEqual(g.player_x,g.fall_target_x)
        self.assertEqual(g.lift,-g.fall_ground_drop)
    def test_ground_impact_and_sitting_dirty_bounds_cover_both_characters(self):
        lib.nabo_jump_character_bounds.argtypes=[ctypes.POINTER(Game),ctypes.c_uint32]+[ctypes.POINTER(ctypes.c_int)]*4
        for character, direction in [(0,1),(0,-1),(1,1),(1,-1)]:
            g=reset();g.character=character;g.dir=direction
            release=throw(g,hold=900);step(g,release+400)
            previous=render(g,release+400);last=(Game.from_buffer_copy(g),release+400)
            for age in range(40,801,40):
                now=release+400+age;step(g,now);current=render(g,now)
                boxes=[]
                for game,when in [last,(g,now)]:
                    vals=[ctypes.c_int() for _ in range(4)]
                    lib.nabo_jump_character_bounds(ctypes.byref(game),when,*[ctypes.byref(v) for v in vals])
                    boxes.append([v.value for v in vals])
                x0,y0=min(b[0] for b in boxes),min(b[1] for b in boxes)
                x1,y1=max(b[2] for b in boxes),max(b[3] for b in boxes)
                before=previous.load();after=current.load()
                for y in range(320):
                    for x in range(240):
                        if before[x,y]!=after[x,y]:self.assertTrue(x0<=x<=x1 and y0<=y<=y1,(character,direction,age,x,y))
                previous=current;last=(Game.from_buffer_copy(g),now)
    def test_charge_clamp_and_clock_wrap(self):
        g=reset();throw(g,hold=10000);self.assertEqual(g.distance,214)
        g=reset();lib.nabo_jump_press(ctypes.byref(g),0xfffffff0);lib.nabo_jump_release(ctypes.byref(g),504);self.assertEqual(g.distance,82)
    def test_landing_stands_before_camera_and_failed_character_stays_on_screen(self):
        lib.nabo_jump_camera_moving.argtypes=[ctypes.POINTER(Game),ctypes.c_uint32]
        lib.nabo_jump_camera_moving.restype=ctypes.c_bool
        g=reset();release=throw(g);step(g,release+400)
        self.assertFalse(lib.nabo_jump_camera_moving(ctypes.byref(g),release+600))
        self.assertTrue(lib.nabo_jump_camera_moving(ctypes.byref(g),release+660))
        for hold in [200,760,1400]:
            g=reset();release=throw(g,hold=hold);step(g,release+400)
            step(g,release+1200);self.assertEqual(g.phase,5)
            screen_x=120+g.player_x-g.dir*g.gap//2
            self.assertTrue(30<=screen_x<=210)
            foot=216+g.player_y-3-g.lift
            self.assertTrue(215<=foot<=245)
    def test_no_extra_charge_during_flight(self):
        g=reset();release=throw(g);distance=g.distance;lib.nabo_jump_press(ctypes.byref(g),release+100);self.assertEqual(g.phase,2);self.assertEqual(g.distance,distance)
    def test_held_charge_starts_during_landing_and_preserves_input_time(self):
        g=reset();release=throw(g);step(g,release+400)
        lib.nabo_jump_press(ctypes.byref(g),release+420)
        self.assertTrue(g.pending_charge)
        step(g,release+920)
        self.assertEqual(g.phase,1);self.assertEqual(g.charge_at,release+420)
        lib.nabo_jump_release(ctypes.byref(g),release+1000)
        self.assertEqual(g.phase,2);self.assertEqual(g.distance,91)
    def test_early_release_queued_during_camera_starts_flight_without_skipping_frames(self):
        g=reset();release=throw(g);step(g,release+400)
        lib.nabo_jump_press(ctypes.byref(g),release+500)
        lib.nabo_jump_release(ctypes.byref(g),release+800)
        self.assertTrue(g.pending_released);self.assertEqual(g.pending_hold,300)
        step(g,release+920)
        self.assertEqual(g.phase,2);self.assertEqual(g.distance,49)
        self.assertEqual(g.changed_at,release+920);self.assertEqual(g.charge_at,release+620)
        self.assertFalse(g.pending_charge);self.assertFalse(g.pending_released)
    def test_fast_next_charge_reduces_settle_without_losing_the_press(self):
        g=reset();release=throw(g);step(g,release+400)
        lib.nabo_jump_press(ctypes.byref(g),release+420)
        step(g,release+769);self.assertEqual(g.phase,3)
        step(g,release+770);self.assertEqual(g.phase,1)
        self.assertEqual(g.charge_at,release+420)
    def test_queued_sound_stops_with_a_short_silence_transition(self):
        sound=Sound();lib.nabo_jump_sound_start(ctypes.byref(sound),5,1)
        data=(ctypes.c_int16*320)();lib.nabo_jump_sound_fill(ctypes.byref(sound),data,320)
        last=data[-1];lib.nabo_jump_sound_start(ctypes.byref(sound),6,1)
        lib.nabo_jump_sound_fill(ctypes.byref(sound),data,320)
        self.assertEqual(data[0],last)
        self.assertTrue(all(v==0 for v in data[128:]))
    def test_clipped_rows_equal_full_composition(self):
        for seed in range(12):
            g=reset(seed);g.skin=seed%6;g.character=seed%2
            image=render(g,1000)
            self.assertEqual(image.size,(240,320))
            for y in [0,25,100,180,215,319]:
                whole=(ctypes.c_uint16*240)();part=(ctypes.c_uint16*81)();lib.nabo_jump_draw_row(ctypes.byref(g),1000,y,0,239,whole);lib.nabo_jump_draw_row(ctypes.byref(g),1000,y,60,140,part)
                self.assertEqual(list(part),list(whole)[60:141])

def hold_for(distance):
    # Smallest hold whose integer flight distance equals `distance`.
    hold=max(0,(distance-4)*1000//150)
    while 4+hold*150//1000<distance:hold+=1
    return hold

class Difficulty(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        lib.nabo_jump_difficulty.argtypes=[ctypes.c_int]+[ctypes.POINTER(ctypes.c_int)]*4
        lib.nabo_jump_tolerance.restype=lib.nabo_jump_center_tolerance.restype=ctypes.c_int
    def curve(self,steps):
        v=[ctypes.c_int() for _ in range(4)];lib.nabo_jump_difficulty(steps,*[ctypes.byref(x) for x in v]);return [x.value for x in v]
    def test_curve_starts_unchanged_then_tightens_monotonically(self):
        self.assertEqual(self.curve(0),[65,94,0,0]);self.assertEqual(self.curve(6),[65,94,0,0])
        last=self.curve(0)
        for steps in range(1,200):
            now=self.curve(steps)
            for a,b in zip(last,now):self.assertGreaterEqual(b,a)
            self.assertLessEqual(now[1],110);self.assertLessEqual(now[2]+now[3],80);self.assertLess(now[0],now[1])
            last=now
        self.assertGreater(self.curve(40)[3],0);self.assertEqual(self.curve(200),[73,110,35,45])
    def test_tolerances_shrink_with_platform_size(self):
        self.assertEqual([lib.nabo_jump_tolerance(i) for i in range(3)],[16,11,9])
        self.assertEqual([lib.nabo_jump_center_tolerance(i) for i in range(3)],[5,4,3])
    def test_landing_window_follows_target_size(self):
        for size,tol in [(0,16),(1,11),(2,9)]:
            for delta,lands in [(tol,True),(tol+1,False),(-tol,True),(-tol-1,False)]:
                g=reset();g.next_size=size;g.gap=90
                release=throw(g,hold=hold_for(90+delta));step(g,release+400)
                self.assertEqual(g.landed,lands,(size,delta));self.assertEqual(g.phase,3 if lands else 4)
    def test_cup_is_worth_an_extra_point_and_sizes_advance(self):
        g=reset();g.next_size=2;g.gap=80;release=throw(g,hold=hold_for(80+6));step(g,release+400)
        self.assertTrue(g.landed);self.assertFalse(g.centered);self.assertEqual(g.score,2);self.assertEqual(g.steps,1)
        future=g.future_size;step(g,release+920)
        self.assertEqual((g.phase,g.size,g.next_size),(0,2,future))
    def test_long_perfect_run_reaches_cups_and_wider_gaps(self):
        g=reset(seed=5);now=1000;sizes=[];gaps=[]
        for i in range(80):
            gaps.append(g.gap);sizes.append(g.next_size)
            release=throw(g,now,hold=hold_for(g.gap));step(g,release+400)
            self.assertTrue(g.landed,(i,g.gap,g.next_size,g.distance))
            step(g,release+920);self.assertEqual(g.phase,0);now=release+1000
        self.assertEqual(sizes[:8],[0]*8);self.assertTrue(all(65<=x<=110 for x in gaps))
        self.assertIn(2,sizes[20:]);self.assertIn(1,sizes);self.assertGreater(max(gaps[40:]),94)
    def test_small_and_cup_render_and_clip_consistently(self):
        for size in (1,2):
            for seed in range(6):
                g=reset(seed);g.next_size=size;g.size=size;g.next_skin=seed%6;g.character=seed%2
                image=render(g,1000);px=image.load()
                cx=120+g.dir*g.gap-g.dir*g.gap//2;cy=216-g.gap//2
                self.assertNotEqual(px[cx,cy],px[2,2])  # something drawn at the top centre
                for y in range(0,320,7):
                    whole=(ctypes.c_uint16*240)();part=(ctypes.c_uint16*81)()
                    lib.nabo_jump_draw_row(ctypes.byref(g),1000,y,0,239,whole);lib.nabo_jump_draw_row(ctypes.byref(g),1000,y,60,140,part)
                    self.assertEqual(list(part),list(whole)[60:141])
    def test_scene_bounds_cover_small_and_cup_camera_moves(self):
        lib.nabo_jump_scene_bounds.argtypes=[ctypes.POINTER(Game),ctypes.c_uint32]+[ctypes.POINTER(ctypes.c_int)]*4
        for size in (1,2):
            g=reset(3);g.next_size=size;release=throw(g,hold=hold_for(g.gap));step(g,release+400)
            g.future_size=size;previous=render(g,release+400);last=(Game.from_buffer_copy(g),release+400)
            for t in range(release+440,release+940,40):
                step(g,t);current=render(g,t);boxes=[]
                for game,when in [last,(g,t)]:
                    vals=[ctypes.c_int() for _ in range(4)]
                    lib.nabo_jump_scene_bounds(ctypes.byref(game),when,*[ctypes.byref(v) for v in vals]);boxes.append([v.value for v in vals])
                x0,y0=min(b[0] for b in boxes),min(b[1] for b in boxes);x1,y1=max(b[2] for b in boxes),max(b[3] for b in boxes)
                before=previous.load();after=current.load()
                for y in range(320):
                    for x in range(240):
                        if before[x,y]!=after[x,y]:self.assertTrue(x0<=x<=x1 and y0<=y<=y1,(size,t,x,y))
                previous=current;last=(Game.from_buffer_copy(g),t)

def previews():
    states=[]
    g=reset(best=24);states.append(('NABO',g,1000))
    classic=reset(best=24);classic.character=1;states.append(('CLASSIC',classic,1000))
    airborne=reset(best=24);release=throw(airborne);step(airborne,release+280);states.append(('JUMP',airborne,release+280))
    font=ImageFont.truetype('DejaVuSans-Bold.ttf',28)
    small=ImageFont.truetype('DejaVuSans.ttf',14)
    sheet=Image.new('RGB',(720,320))
    for i,(name,game,t) in enumerate(states):
        image=render(game,t);draw=ImageDraw.Draw(image)
        draw.text((16,12),str(game.score),font=font,fill='#344251')
        draw.rounded_rectangle((148,16,226,42),radius=12,fill='#edf0f7')
        draw.text((157,21),'BEST 24',font=small,fill='#65718a')
        draw.text((12,286),'按住 OK 蓄力，松开起跳\n上：返回  下：换角色',font=small,fill='#596779',spacing=2)
        sheet.paste(image,(i*240,0))
    sheet.save(ROOT/'game-art/game-preview.png')

if __name__=='__main__':
    suite=unittest.TestSuite([unittest.defaultTestLoader.loadTestsFromTestCase(Tests),unittest.defaultTestLoader.loadTestsFromTestCase(Difficulty)])
    result=unittest.TextTestRunner(verbosity=2).run(suite)
    import _ctypes
    unload_library(lib._handle);temp.cleanup()
    (ROOT/'jump-validation.json').write_text(json.dumps({'actual_C_game_tests':result.testsRun,'pass':result.wasSuccessful(),'sprites_bytes':len(blob),'physical_buttons':'PENDING'},indent=2))
    raise SystemExit(0 if result.wasSuccessful() else 1)
